#include "OBJLoader.h"

#include <tiny_obj_loader.h>

#include <iostream>
#include <stdexcept>

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

    MeshData meshData;

    for (const tinyobj::shape_t& shape : shapes)
    {
        for (const tinyobj::index_t& index : shape.mesh.indices)
        {
            if (index.vertex_index < 0)
            {
                throw std::runtime_error(
                    "OBJ contains an invalid vertex index."
                );
            }

            const size_t vertexOffset =
                static_cast<size_t>(index.vertex_index) * 3;

            if (vertexOffset + 2 >= attrib.vertices.size())
            {
                throw std::runtime_error(
                    "OBJ vertex index is outside the vertex array."
                );
            }

            Vertex vertex{};

            // Position
            vertex.position[0] =
                attrib.vertices[3 * index.vertex_index + 0];

            vertex.position[1] =
                attrib.vertices[3 * index.vertex_index + 1];

            vertex.position[2] =
                attrib.vertices[3 * index.vertex_index + 2];

            // Temporary color
            vertex.color[0] = 1.0f;
            vertex.color[1] = 1.0f;
            vertex.color[2] = 1.0f;

            // Normal
            if (index.normal_index >= 0)
            {
                vertex.normal[0] =
                    attrib.normals[3 * index.normal_index + 0];

                vertex.normal[1] =
                    attrib.normals[3 * index.normal_index + 1];

                vertex.normal[2] =
                    attrib.normals[3 * index.normal_index + 2];
            }
            else
            {
                vertex.normal[0] = 0.0f;
                vertex.normal[1] = 1.0f;
                vertex.normal[2] = 0.0f;
            }

            // UV
            if (index.texcoord_index >= 0)
            {
                vertex.texCoord[0] =
                    attrib.texcoords[2 * index.texcoord_index + 0];

                vertex.texCoord[1] =
                    1.0f -
                    attrib.texcoords[2 * index.texcoord_index + 1];
            }
            else
            {
                vertex.texCoord[0] = 0.0f;
                vertex.texCoord[1] = 0.0f;
            }

            meshData.vertices.push_back(vertex);

            meshData.indices.push_back(
                static_cast<uint32_t>(meshData.indices.size())
            );
        }
    }

    if (meshData.vertices.empty() || meshData.indices.empty())
    {
        throw std::runtime_error(
            "OBJ contains no drawable triangle data: " + filepath
        );
    }

    std::cout << "Loaded OBJ: " << filepath << std::endl;
    std::cout << "  Vertices: " << meshData.vertices.size() << std::endl;
    std::cout << "  Indices:  " << meshData.indices.size() << std::endl;
    std::cout << "  Vertex stride: "
        << sizeof(Vertex)
        << " bytes"
        << std::endl;

    return meshData;
}