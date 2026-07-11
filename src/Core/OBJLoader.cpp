#include "OBJLoader.h"

#include <tiny_obj_loader.h>

#include <iostream>
#include <stdexcept>
#include <vector>

MeshData OBJLoader::Load(const std::string& filepath)
{
    tinyobj::ObjReaderConfig config;

    config.triangulate = true;

    const size_t slashPosition = filepath.find_last_of("/\\");

    if (slashPosition != std::string::npos)
    {
        config.mtl_search_path = filepath.substr(0, slashPosition + 1);
    }

    tinyobj::ObjReader reader;

    if (!reader.ParseFromFile(filepath, config))
    {
        std::string message = "Failed to load OBJ: " + filepath;

        if (!reader.Error().empty())
        {
            message += "\n" + reader.Error();
        }

        throw std::runtime_error(message);
    }

    if (!reader.Warning().empty())
    {
        std::cout << "[OBJ Warning] "
            << reader.Warning()
            << std::endl;
    }

    const tinyobj::attrib_t& attrib = reader.GetAttrib();
    const std::vector<tinyobj::shape_t>& shapes = reader.GetShapes();
    std::cout << "Shapes: " << shapes.size() << std::endl;

    for (size_t i = 0; i < shapes.size(); ++i)
    {
        std::cout
            << "  Shape " << i
            << ": " << shapes[i].name
            << " | Faces: "
            << shapes[i].mesh.num_face_vertices.size()
            << " | Indices: "
            << shapes[i].mesh.indices.size()
            << std::endl;
    }
    const std::vector<tinyobj::material_t>& materials = reader.GetMaterials();

    std::cout << "Materials: "
        << materials.size()
        << std::endl;

    for (size_t i = 0; i < materials.size(); ++i)
    {
        std::cout
            << "  Material " << i
            << ": " << materials[i].name
            << std::endl;

        std::cout
            << "    Diffuse Texture: "
            << materials[i].diffuse_texname
            << std::endl;

        std::cout
            << "    Diffuse Color: "
            << materials[i].diffuse[0] << ", "
            << materials[i].diffuse[1] << ", "
            << materials[i].diffuse[2]
            << std::endl;
    }

    std::vector<size_t> materialFaceCounts(
        materials.size(),
        0
    );

    for (const tinyobj::shape_t& shape : shapes)
    {
        for (int materialId : shape.mesh.material_ids)
        {
            if (materialId >= 0 &&
                static_cast<size_t>(materialId) < materialFaceCounts.size())
            {
                materialFaceCounts[materialId]++;
            }
        }
    }

    for (size_t i = 0; i < materialFaceCounts.size(); ++i)
    {
        std::cout
            << "  Material " << i
            << " Face Count: "
            << materialFaceCounts[i]
            << std::endl;
    }

    MeshData meshData;

    meshData.materials.resize(materials.size());

    for (size_t i = 0; i < materials.size(); ++i)
    {
        meshData.materials[i].diffuseColor[0] =
            materials[i].diffuse[0];

        meshData.materials[i].diffuseColor[1] =
            materials[i].diffuse[1];

        meshData.materials[i].diffuseColor[2] =
            materials[i].diffuse[2];

        meshData.materials[i].hasDiffuseTexture =
            !materials[i].diffuse_texname.empty();
    }

    bool hasActiveSubMesh = false;
    int activeMaterialId = -1;
    SubMesh activeSubMesh{};

    for (const tinyobj::shape_t& shape : shapes)
    {
        size_t indexOffset = 0;

        for (size_t faceIndex = 0;
            faceIndex < shape.mesh.num_face_vertices.size();
            ++faceIndex)
        {
            const size_t faceVertexCount =
                static_cast<size_t>(
                    shape.mesh.num_face_vertices[faceIndex]
                    );

            if (indexOffset + faceVertexCount >
                shape.mesh.indices.size())
            {
                throw std::runtime_error(
                    "OBJ face indices exceed available index data."
                );
            }

            int faceMaterialId = -1;

            if (faceIndex < shape.mesh.material_ids.size())
            {
                faceMaterialId =
                    shape.mesh.material_ids[faceIndex];
            }

            // Start a new contiguous material range when the material changes.
            if (!hasActiveSubMesh ||
                faceMaterialId != activeMaterialId)
            {
                if (hasActiveSubMesh)
                {
                    activeSubMesh.indexCount =
                        static_cast<uint32_t>(
                            meshData.indices.size()
                            ) -
                        activeSubMesh.firstIndex;

                    if (activeSubMesh.indexCount > 0)
                    {
                        meshData.subMeshes.push_back(
                            activeSubMesh
                        );
                    }
                }

                activeMaterialId = faceMaterialId;

                activeSubMesh = {};
                activeSubMesh.firstIndex =
                    static_cast<uint32_t>(
                        meshData.indices.size()
                        );

                activeSubMesh.materialId =
                    faceMaterialId;

                hasActiveSubMesh = true;
            }

            for (size_t vertexInFace = 0;
                vertexInFace < faceVertexCount;
                ++vertexInFace)
            {
                const size_t sourceIndex =
                    indexOffset + vertexInFace;

                const tinyobj::index_t index =
                    shape.mesh.indices[sourceIndex];

                if (index.vertex_index < 0)
                {
                    throw std::runtime_error(
                        "OBJ contains an invalid vertex index."
                    );
                }

                const size_t positionOffset =
                    static_cast<size_t>(
                        index.vertex_index
                        ) * 3;

                if (positionOffset + 2 >=
                    attrib.vertices.size())
                {
                    throw std::runtime_error(
                        "OBJ vertex index exceeds position data."
                    );
                }

                Vertex vertex{};

                vertex.position[0] =
                    attrib.vertices[positionOffset + 0];

                vertex.position[1] =
                    attrib.vertices[positionOffset + 1];

                vertex.position[2] =
                    attrib.vertices[positionOffset + 2];

                // Use the material's diffuse colour as the fallback colour.
                if (faceMaterialId >= 0 &&
                    static_cast<size_t>(faceMaterialId) <
                    meshData.materials.size())
                {
                    const MaterialData& material =
                        meshData.materials[faceMaterialId];

                    vertex.color[0] =
                        material.diffuseColor[0];

                    vertex.color[1] =
                        material.diffuseColor[1];

                    vertex.color[2] =
                        material.diffuseColor[2];
                }
                else
                {
                    vertex.color[0] = 1.0f;
                    vertex.color[1] = 1.0f;
                    vertex.color[2] = 1.0f;
                }

                if (index.normal_index >= 0)
                {
                    const size_t normalOffset =
                        static_cast<size_t>(
                            index.normal_index
                            ) * 3;

                    if (normalOffset + 2 >=
                        attrib.normals.size())
                    {
                        throw std::runtime_error(
                            "OBJ normal index exceeds normal data."
                        );
                    }

                    vertex.normal[0] =
                        attrib.normals[normalOffset + 0];

                    vertex.normal[1] =
                        attrib.normals[normalOffset + 1];

                    vertex.normal[2] =
                        attrib.normals[normalOffset + 2];
                }
                else
                {
                    vertex.normal[0] = 0.0f;
                    vertex.normal[1] = 1.0f;
                    vertex.normal[2] = 0.0f;
                }

                if (index.texcoord_index >= 0)
                {
                    const size_t texCoordOffset =
                        static_cast<size_t>(
                            index.texcoord_index
                            ) * 2;

                    if (texCoordOffset + 1 >=
                        attrib.texcoords.size())
                    {
                        throw std::runtime_error(
                            "OBJ UV index exceeds texture coordinate data."
                        );
                    }

                    vertex.texCoord[0] =
                        attrib.texcoords[texCoordOffset + 0];

                    vertex.texCoord[1] =
                        1.0f -
                        attrib.texcoords[texCoordOffset + 1];
                }
                else
                {
                    vertex.texCoord[0] = 0.0f;
                    vertex.texCoord[1] = 0.0f;
                }

                meshData.vertices.push_back(vertex);

                meshData.indices.push_back(
                    static_cast<uint32_t>(
                        meshData.indices.size()
                        )
                );
            }

            indexOffset += faceVertexCount;
        }
    }

    // Flush the final contiguous material range.
    if (hasActiveSubMesh)
    {
        activeSubMesh.indexCount =
            static_cast<uint32_t>(
                meshData.indices.size()
                ) -
            activeSubMesh.firstIndex;

        if (activeSubMesh.indexCount > 0)
        {
            meshData.subMeshes.push_back(
                activeSubMesh
            );
        }
    }

    std::cout << "Loaded OBJ: " << filepath << std::endl;
    std::cout << "  Vertices: " << meshData.vertices.size() << std::endl;
    std::cout << "  Indices:  " << meshData.indices.size() << std::endl;
    std::cout << "  Vertex stride: " << sizeof(Vertex) << " bytes" << std::endl;

    std::cout << "  SubMeshes: "
        << meshData.subMeshes.size()
        << std::endl;

    for (const SubMesh& subMesh : meshData.subMeshes)
    {
        std::cout
            << "    Material "
            << subMesh.materialId
            << " | First Index: "
            << subMesh.firstIndex
            << " | Index Count: "
            << subMesh.indexCount
            << std::endl;
    }
    return meshData;
}
