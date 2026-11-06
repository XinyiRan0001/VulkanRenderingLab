#include "RoughReflectionFilter.h"
#include "ShaderUtils.h"
#include <array>
#include <stdexcept>
#include <cmath>
#include <iostream>
#include <vector>
#include <algorithm>
#include <fstream>
namespace {
void Check(VkResult r){if(r!=VK_SUCCESS)throw std::runtime_error("Cannot create rough reflection filter: "+std::to_string(r));}
struct Camera { glm::mat4 inverseProjection,inverseView;glm::ivec4 size,sequence; };
static_assert(sizeof(Camera)==160,"Reflection camera std140 mismatch");
VkPipeline Pipeline(VkDevice d,VkPipelineLayout layout,const char* path){
    auto bytes=ReadFile(path);
    VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    module.codeSize=bytes.size();module.pCode=reinterpret_cast<const uint32_t*>(bytes.data());
    VkShaderModule shader;Check(vkCreateShaderModule(d,&module,nullptr,&shader));
    VkComputePipelineCreateInfo create{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    create.layout=layout;create.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    create.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;create.stage.module=shader;create.stage.pName="main";
    VkPipeline result;auto status=vkCreateComputePipelines(d,VK_NULL_HANDLE,1,&create,nullptr,&result);
    vkDestroyShaderModule(d,shader,nullptr);Check(status);return result;
}
}
RoughReflectionFilter::RoughReflectionFilter(const PhysicalDevice& physical,const LogicalDevice& device,
    VkExtent2D extent,VkDescriptorSetLayout cameraLayout,VkDescriptorSetLayout rayLayout,
    VkImageView normal,VkImageView depth,VkSampler sampler,bool verify):m_Device(device),m_Extent(extent){
    auto d=device.GetHandle();
    VkFormatProperties props;vkGetPhysicalDeviceFormatProperties(physical.GetHandle(),VK_FORMAT_R32G32B32A32_SFLOAT,&props);
    const auto needed=VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if((props.optimalTilingFeatures&needed)!=needed)throw std::runtime_error("Rough reflection filtering requires sampled/storage/transfer RGBA32F");
    try {
        const auto image=[&]{return std::make_unique<Image>(physical,device,extent.width,extent.height,VK_FORMAT_R32G32B32A32_SFLOAT,VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,VK_IMAGE_ASPECT_COLOR_BIT);};
        m_Raw=image();m_Ping=image();m_Filtered=image();m_HitGuide=image();m_Accumulated=image();m_HistoryImage=image();
        if(verify)m_Readback=std::make_unique<Buffer>(physical,device,VkDeviceSize(extent.width)*extent.height*64,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        m_Camera=std::make_unique<Buffer>(physical,device,sizeof(Camera),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkDescriptorSetLayoutBinding bindings[9]{};
        for(int i=0;i<9;++i){bindings[i].binding=i;bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
            bindings[i].descriptorType=(i<3 || i==6)?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:i==5?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;}
        VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};set.bindingCount=9;set.pBindings=bindings;
        Check(vkCreateDescriptorSetLayout(d,&set,nullptr,&m_SetLayout));
        VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,4},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,4},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=1;pool.poolSizeCount=3;pool.pPoolSizes=sizes;
        Check(vkCreateDescriptorPool(d,&pool,nullptr,&m_Pool));
        VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};alloc.descriptorPool=m_Pool;alloc.descriptorSetCount=1;alloc.pSetLayouts=&m_SetLayout;
        Check(vkAllocateDescriptorSets(d,&alloc,&m_Set));
        VkDescriptorImageInfo images[]={{VK_NULL_HANDLE,m_Raw->GetView(),VK_IMAGE_LAYOUT_GENERAL},
            {VK_NULL_HANDLE,m_Ping->GetView(),VK_IMAGE_LAYOUT_GENERAL},{VK_NULL_HANDLE,m_Filtered->GetView(),VK_IMAGE_LAYOUT_GENERAL},
            {sampler,normal,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{sampler,depth,VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
            {},{VK_NULL_HANDLE,m_HitGuide->GetView(),VK_IMAGE_LAYOUT_GENERAL},
            {sampler,m_HitGuide->GetView(),VK_IMAGE_LAYOUT_GENERAL},
            {sampler,m_Accumulated->GetView(),VK_IMAGE_LAYOUT_GENERAL}};
        VkDescriptorBufferInfo buffer{m_Camera->GetHandle(),0,sizeof(Camera)};
        VkWriteDescriptorSet writes[9]{};
        for(int i=0;i<9;++i){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=m_Set;writes[i].dstBinding=i;
            writes[i].descriptorCount=1;writes[i].descriptorType=bindings[i].descriptorType;
            if(i!=5)writes[i].pImageInfo=&images[i];else writes[i].pBufferInfo=&buffer;}
        vkUpdateDescriptorSets(d,9,writes,0,nullptr);
        VkDescriptorSetLayout layouts[]={cameraLayout,rayLayout,m_SetLayout};
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=3;layout.pSetLayouts=layouts;
        Check(vkCreatePipelineLayout(d,&layout,nullptr,&m_TraceLayout));
        VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,4};
        layout.setLayoutCount=1;layout.pSetLayouts=&m_SetLayout;layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&push;
        Check(vkCreatePipelineLayout(d,&layout,nullptr,&m_FilterLayout));
        m_Trace=Pipeline(d,m_TraceLayout,"build/shaders/rough_trace.comp.spv");
        m_Filter=Pipeline(d,m_FilterLayout,"build/shaders/rough_filter.comp.spv");
        VkDescriptorSetLayoutBinding temporalBindings[]={
            {0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
            {1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
            {2,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
            {3,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
        set.bindingCount=4;set.pBindings=temporalBindings;
        Check(vkCreateDescriptorSetLayout(d,&set,nullptr,&m_TemporalSetLayout));
        VkDescriptorPoolSize temporalSizes[]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1}};
        pool.pPoolSizes=temporalSizes;
        Check(vkCreateDescriptorPool(d,&pool,nullptr,&m_TemporalPool));
        alloc.descriptorPool=m_TemporalPool;alloc.pSetLayouts=&m_TemporalSetLayout;
        Check(vkAllocateDescriptorSets(d,&alloc,&m_TemporalSet));
        VkDescriptorImageInfo temporalImages[]={
            {sampler,m_Raw->GetView(),VK_IMAGE_LAYOUT_GENERAL},
            {sampler,m_HistoryImage->GetView(),VK_IMAGE_LAYOUT_GENERAL},
            {VK_NULL_HANDLE,m_Accumulated->GetView(),VK_IMAGE_LAYOUT_GENERAL}};
        VkWriteDescriptorSet temporalWrites[4]{};
        for(int i=0;i<4;++i){
            auto& w=temporalWrites[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet=m_TemporalSet;w.dstBinding=i;w.descriptorCount=1;w.descriptorType=temporalBindings[i].descriptorType;
            if(i<3)w.pImageInfo=&temporalImages[i];else w.pBufferInfo=&buffer;
        }
        vkUpdateDescriptorSets(d,4,temporalWrites,0,nullptr);
        layout.pSetLayouts=&m_TemporalSetLayout;layout.pushConstantRangeCount=0;layout.pPushConstantRanges=nullptr;
        Check(vkCreatePipelineLayout(d,&layout,nullptr,&m_TemporalLayout));
        m_Temporal=Pipeline(d,m_TemporalLayout,"build/shaders/rough_temporal.comp.spv");
        InitializeHistory(physical);
    }catch(...){Release();throw;}
}
RoughReflectionFilter::~RoughReflectionFilter(){Release();}
void RoughReflectionFilter::Release(){
    auto d=m_Device.GetHandle();
    if(m_Temporal)vkDestroyPipeline(d,m_Temporal,nullptr);
    if(m_TemporalLayout)vkDestroyPipelineLayout(d,m_TemporalLayout,nullptr);
    if(m_TemporalPool)vkDestroyDescriptorPool(d,m_TemporalPool,nullptr);
    if(m_TemporalSetLayout)vkDestroyDescriptorSetLayout(d,m_TemporalSetLayout,nullptr);
    if(m_Trace)vkDestroyPipeline(d,m_Trace,nullptr);if(m_Filter)vkDestroyPipeline(d,m_Filter,nullptr);
    if(m_TraceLayout)vkDestroyPipelineLayout(d,m_TraceLayout,nullptr);if(m_FilterLayout)vkDestroyPipelineLayout(d,m_FilterLayout,nullptr);
    if(m_Pool)vkDestroyDescriptorPool(d,m_Pool,nullptr);if(m_SetLayout)vkDestroyDescriptorSetLayout(d,m_SetLayout,nullptr);
}
void RoughReflectionFilter::Update(const UniformBufferObject& ubo,glm::vec4 pose,glm::ivec4 renderState){
    m_LastCamera=ubo;
    ReflectionHistoryState::Snapshot snapshot{};
    size_t index=0;
    const auto append=[&](glm::vec4 v){for(int i=0;i<4;++i)snapshot[index++]=v[i];};
    for(auto matrix:{ubo.model,ubo.view,ubo.proj,ubo.lightViewProj})
        for(int column=0;column<4;++column)append(matrix[column]);
    append(glm::vec4(ubo.shadowSettings));append(ubo.pointLight.position);append(ubo.pointLight.color);
    append(ubo.cameraPosition);append(ubo.lightingSettings);append(pose);
    append(glm::vec4(float(m_Samples),m_Furnace?1.f:0.f,m_HitGuidance?1.f:0.f,m_History.Enabled()?1.f:0.f));
    append(glm::vec4(renderState));
    if(index!=snapshot.size())throw std::runtime_error("Reflection history signature size mismatch");
    const auto frame=m_History.Prepare(snapshot);
    Camera camera{glm::inverse(ubo.proj),glm::inverse(ubo.view),
        glm::ivec4(m_Extent.width,m_Extent.height,m_HitGuidance?1:0,m_Furnace?-m_Samples:m_Samples),
        glm::ivec4(frame.phase,frame.enabled?1:0,frame.previous,frame.reset?1:0)};
    m_Camera->UploadData(&camera,sizeof(camera));
}
void RoughReflectionFilter::Record(VkCommandBuffer cmd,VkDescriptorSet camera,VkDescriptorSet rays,VkQueryPool pool) const{
    vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,pool,22);
    std::array<VkImageMemoryBarrier,5> images{};
    const VkImage handles[]={m_Raw->GetHandle(),m_Ping->GetHandle(),m_Filtered->GetHandle(),m_HitGuide->GetHandle(),m_Accumulated->GetHandle()};
    for(int i=0;i<5;++i){
        auto& b=images[i];b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;b.newLayout=VK_IMAGE_LAYOUT_GENERAL;
        b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        b.image=handles[i];b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    }
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,5,images.data());
    VkDescriptorSet sets[]={camera,rays,m_Set};
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,m_Trace);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,m_TraceLayout,0,3,sets,0,nullptr);
    vkCmdDispatch(cmd,(m_Extent.width+7)/8,(m_Extent.height+7)/8,1);
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
    const auto sync=[&](VkPipelineStageFlags dst){vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,dst,0,1,&barrier,0,nullptr,0,nullptr);};
    sync(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,pool,23);
    vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,pool,26);
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,m_Temporal);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,m_TemporalLayout,0,1,&m_TemporalSet,0,nullptr);
    vkCmdDispatch(cmd,(m_Extent.width+7)/8,(m_Extent.height+7)/8,1);
    VkImageMemoryBarrier copies[2]{};
    const VkImage historyImages[]={m_Accumulated->GetHandle(),m_HistoryImage->GetHandle()};
    for(int i=0;i<2;++i){
        auto& b=copies[i];b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        b.dstAccessMask=i==0?VK_ACCESS_TRANSFER_READ_BIT:VK_ACCESS_TRANSFER_WRITE_BIT;
        b.oldLayout=VK_IMAGE_LAYOUT_GENERAL;
        b.newLayout=i==0?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        b.image=historyImages[i];b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    }
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,2,copies);
    VkImageCopy copy{};copy.srcSubresource=copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.extent={m_Extent.width,m_Extent.height,1};
    vkCmdCopyImage(cmd,historyImages[0],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,historyImages[1],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
    for(int i=0;i<2;++i){
        auto& b=copies[i];std::swap(b.oldLayout,b.newLayout);b.srcAccessMask=b.dstAccessMask;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    }
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,2,copies);
    vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,pool,27);
    vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,pool,24);
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,m_Filter);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,m_FilterLayout,0,1,&m_Set,0,nullptr);
    for(int step:{1,2,4}){
        vkCmdPushConstants(cmd,m_FilterLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,4,&step);
        vkCmdDispatch(cmd,(m_Extent.width+7)/8,(m_Extent.height+7)/8,1);
        sync(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }
    sync(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,pool,25);
    if(m_Readback){
        const VkImage outputs[]={m_Raw->GetHandle(),m_Filtered->GetHandle(),m_HitGuide->GetHandle(),m_Accumulated->GetHandle()};
        for(int i=0;i<4;++i){
            VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            b.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
            b.oldLayout=VK_IMAGE_LAYOUT_GENERAL;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
            b.image=outputs[i];b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
            VkBufferImageCopy copy{};copy.bufferOffset=VkDeviceSize(i)*m_Extent.width*m_Extent.height*16;
            copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={m_Extent.width,m_Extent.height,1};
            vkCmdCopyImageToBuffer(cmd,outputs[i],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,m_Readback->GetHandle(),1,&copy);
            b.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
            b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_GENERAL;
            vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&b);
        }
        VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);
    }
}

void RoughReflectionFilter::ReadCompleted(){
    if(!m_Pending)return;m_Pending=false;
    const size_t count=size_t(m_Extent.width)*m_Extent.height;
    std::vector<glm::vec4> data(count*4);
    m_Readback->DownloadData(data.data(),m_Readback->GetSize());
    size_t geometry=0,sky=0;
    size_t sharp=0,rough=0,invalid=0,pairs=0;float sharpError=0;
    double rawVariation=0,filteredVariation=0;
    for(size_t i=0;i<count;++i){
        const auto raw=data[i],filtered=data[count+i];
        for(int k=0;k<4;++k)if(!std::isfinite(raw[k]) || !std::isfinite(filtered[k]))
            throw std::runtime_error("Nonfinite reflection output");
        const auto guide=data[count*2+i];
        for(int k=0;k<4;++k)if(!std::isfinite(guide[k]))throw std::runtime_error("Nonfinite reflection hit guide");
        if(raw.a==0){
            if(glm::length(guide)!=0)throw std::runtime_error("Hit guide outside receiver");
        }else if(guide.w>0){
            ++geometry;
            if(std::abs(glm::length(glm::vec3(guide))-1.f)>.002f)throw std::runtime_error("Nonunit reflected hit normal");
        }else{
            ++sky;
            if(guide.w!=-1 || glm::length(glm::vec3(guide))!=0)throw std::runtime_error("Invalid reflection environment guide");
        }
        if(raw.a!=filtered.a)throw std::runtime_error("Reflection receiver tag changed");
        if(raw.a==0){
            ++invalid;
            if(glm::length(glm::vec3(filtered))!=0)throw std::runtime_error("Reflection leaked outside receiver");
        }else if(std::abs(raw.a)<.1f){
            ++sharp;for(int k=0;k<3;++k)sharpError=std::max(sharpError,std::abs(raw[k]-filtered[k]));
        }else {
            ++rough;
            if(i%m_Extent.width+1<m_Extent.width && std::abs(std::abs(data[i+1].a)-std::abs(raw.a))<.001f){
                rawVariation+=glm::length(glm::vec3(raw-data[i+1]));
                filteredVariation+=glm::length(glm::vec3(filtered-data[count+i+1]));++pairs;
            }
        }
    }
    float temporalError=0;
    if(m_LastTemporal.previous && m_PreviousMean.size()!=count)
        throw std::runtime_error("Temporal history accepted without a completed predecessor");
    for(size_t i=0;i<count;++i){
        const auto raw=data[i],mean=data[count*3+i];
        glm::vec3 expected(raw);
        if(m_LastTemporal.enabled && m_LastTemporal.previous && std::abs(raw.a)>.1f &&
           std::abs(std::abs(m_PreviousMean[i].a)-std::abs(raw.a))<.001f)
            expected=glm::mix(glm::vec3(m_PreviousMean[i]),glm::vec3(raw),1.f/float(m_LastTemporal.previous+1));
        if(mean.a!=raw.a)throw std::runtime_error("Temporal receiver tag changed");
        for(int k=0;k<3;++k){
            if(!std::isfinite(mean[k]))throw std::runtime_error("Nonfinite reflection history");
            const float error=std::abs(mean[k]-expected[k]);temporalError=std::max(temporalError,error);
            if(error>2e-5f*std::max(1.f,std::abs(expected[k])))throw std::runtime_error("Temporal mean or reset mismatch");
        }
    }
    m_PreviousMean.assign(data.begin()+count*3,data.end());
    std::cout<<"[Reflection Temporal Verify] enabled="<<m_LastTemporal.enabled<<" previous="<<m_LastTemporal.previous
             <<" phase="<<m_LastTemporal.phase<<" reset="<<m_LastTemporal.reset<<" max_error="<<temporalError<<std::endl;
    if(sharpError!=0)throw std::runtime_error("Sharp reflection was filtered");
    if(!m_DumpPath.empty()){
        for(int output=0;output<2;++output){
            std::ofstream file(m_DumpPath+(output==0?".raw.f32":".filtered.f32"),std::ios::binary);
            file.write(reinterpret_cast<const char*>(data.data()+output*count),std::streamsize(count*sizeof(glm::vec4)));
            if(!file)throw std::runtime_error("Cannot write reflection HDR dump");
        }
        std::ofstream temporalFile(m_DumpPath+".temporal.f32",std::ios::binary);
        temporalFile.write(reinterpret_cast<const char*>(data.data()+count*3),std::streamsize(count*sizeof(glm::vec4)));
        if(!temporalFile)throw std::runtime_error("Cannot write reflection temporal dump");
        std::ofstream meta(m_DumpPath+".json");
        meta<<"{\"width\":"<<m_Extent.width<<",\"height\":"<<m_Extent.height
            <<",\"samples\":"<<m_LastSamples<<",\"channels\":4,\"type\":\"float32-little-endian\",\"model\":\"GGX-NDF-Smith-correlated-Schlick-single-scatter\"}";
        // Extend the metadata with the matrices used by this completed frame.
        meta.seekp(-1,std::ios::cur);
        meta<<",\"furnace\":"<<(m_LastFurnace?"true":"false")
            <<",\"temporal_enabled\":"<<(m_LastTemporal.enabled?"true":"false")
            <<",\"temporal_previous\":"<<m_LastTemporal.previous<<",\"phase\":"<<m_LastTemporal.phase;
        const glm::mat4 matrices[]={glm::inverse(m_LastCamera.proj),glm::inverse(m_LastCamera.view)};
        const char* names[]={"inverseProjection","inverseView"};
        for(int matrix=0;matrix<2;++matrix){
            meta<<",\""<<names[matrix]<<"\":[";
            meta.precision(10);
            for(int i=0;i<16;++i){if(i)meta<<",";meta<<matrices[matrix][i/4][i%4];}
            meta<<"]";
        }
        meta<<"}";
        if(!meta)throw std::runtime_error("Cannot write reflection dump metadata");
    }
    std::cout<<"[Reflection Hit Guide Verify] finite=OK normals=OK geometry="<<geometry<<" sky="<<sky
             <<" enabled="<<m_LastHitGuidance<<std::endl;
    std::cout<<"[Reflection Spatial Verify] finite=OK tags=OK invalid="<<invalid<<" sharp="<<sharp
             <<" sharp_error="<<sharpError<<" rough="<<rough<<" pairs="<<pairs
             <<" variation_raw="<<rawVariation/std::max(size_t(1),pairs)
             <<" variation_filtered="<<filteredVariation/std::max(size_t(1),pairs)<<std::endl;
}

void RoughReflectionFilter::SetSamples(int samples){
    if(samples<1 || samples>256)throw std::runtime_error("Reflection samples must be 1..256");
    m_Samples=samples;
}

void RoughReflectionFilter::InitializeHistory(const PhysicalDevice& physical){
    auto d=m_Device.GetHandle();VkCommandPool pool=VK_NULL_HANDLE;
    try{
        VkCommandPoolCreateInfo create{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        create.queueFamilyIndex=physical.FindQueueFamilies().graphicsFamily.value();
        Check(vkCreateCommandPool(d,&create,nullptr,&pool));
        VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool=pool;allocation.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocation.commandBufferCount=1;
        VkCommandBuffer cmd;Check(vkAllocateCommandBuffers(d,&allocation,&cmd));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        Check(vkBeginCommandBuffer(cmd,&begin));
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
        barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.image=m_HistoryImage->GetHandle();barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkClearColorValue clear{};vkCmdClearColorImage(cmd,barrier.image,VK_IMAGE_LAYOUT_GENERAL,&clear,1,&barrier.subresourceRange);
        barrier.oldLayout=VK_IMAGE_LAYOUT_GENERAL;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        Check(vkEndCommandBuffer(cmd));
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;
        Check(vkQueueSubmit(m_Device.GetGraphicsQueue(),1,&submit,VK_NULL_HANDLE));Check(vkQueueWaitIdle(m_Device.GetGraphicsQueue()));
    }catch(...){if(pool)vkDestroyCommandPool(d,pool,nullptr);throw;}
    vkDestroyCommandPool(d,pool,nullptr);
}
