
#include <iostream>
#include <vector>

#include "vk_common.h"

// j'utilise la lib simdjson : il vous faut les deux fichiers à simdjson.h/.cpp dans le repertoire singleheader ici:
// https://github.com/simdjson/simdjson/tree/master/singleheader
#include "../libs/simdjson/simdjson.h"

// dependance sur la glm 
#include <glm/glm/gtx/quaternion.hpp>

//#include "Vertex.h"
// dans un premier temps les normal maps ne sont pas necessaires
// vous pouvez commenter toutes les references a "tangent" ainsi qu'a "normalTexture" 
//struct Vertex
//{
//	glm::vec3 position;			//  3x4 octets = 12
//	glm::vec3 normal;			// +3x4 octets = 24
//	glm::vec2 texcoords;			// +2x4 octets = 32
//	glm::vec4 tangent;   // optionnel, seulement si vous implementez le normal mapping
//};

//#include "Material.h"
// la structure Material est de la forme 
//struct Material
//{
//	glm::vec3 diffuseColor;
//	glm::vec3 emissiveColor;
//	float roughness;	// perceptual
//	float metalness;
//	uint32_t diffuseTexture;
//	uint32_t normalTexture;
//	uint32_t roughnessTexture;
//	uint32_t ambientTexture;
//	uint32_t emissiveTexture;
//
//	static Material defaultMaterial;
//};
//#include "Mesh.h"
// la structure Mesh n'est pas necessaire, mais contient au minimum ceci:
//struct Mesh
//{
//	static bool ParseGLTF(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, Material& material, const char* filepath);
//};

//#include "Texture.h"
// a vous de coder la fonction LoadTexture(const char* filepath, bool sRGB);
// le second parametre indique si les pixels de l'image doivent etre converti de sRGB vers lineaire par le GPU

// materiau par defaut (couleur diffuse, emissive, roughness, metalness, diffuse tex, normal tex, pbr tex, AO tex, emissive
Material Material::defaultMaterial = { { 1.f, 1.f, 1.f }, {0.f, 0.f, 0.f}, 1.f, 1.f, 1, 2, 1, 0, 0 };

struct GLTFBuffer {
	uint8_t* data;
	uint32_t length;
};

template<typename T>
static void ParseGLTFAttribute(const simdjson::dom::element& gltf, const std::vector<GLTFBuffer>& buffers, int32_t id, std::vector<T>& data)
{
	const auto& view = gltf["bufferViews"].at(id);
	const auto& accessor = gltf["accessors"].at(id);
	{
		int64_t bufferId = view["buffer"];
		int64_t offset = 0;
		if (view["byteOffset"].is_int64())
			offset = view["byteOffset"];
		int64_t len = view["byteLength"];
		if (accessor["byteOffset"].is_int64())
			offset += (int64_t)accessor["byteOffset"];
		int64_t componentType = accessor["componentType"];
		uint32_t componentSize = 4;
		if (componentType < 5126)
			componentSize = 1 << ((componentType - 5120) / 2);
		std::string_view type = accessor["type"];
		int64_t count = accessor["count"];
		uint8_t* attribs = buffers[bufferId].data + offset;
		data.resize(len / componentSize);
		memcpy(data.data(), attribs, len);
	}
}

static uint32_t ParseGLTFTexture(const std::string& relativePath, simdjson::dom::element gltf, simdjson::dom::element element, bool sRGB = true, uint32_t default_id = 1)
{
	uint32_t id = default_id;
	if (element.is_object()) {
		int64_t textureId = element["index"];
		int64_t imageId = gltf["textures"].at(textureId)["source"];
		std::string_view uri = gltf["images"].at(imageId)["uri"];
		std::string imagePath = relativePath + uri.begin();
		id = Texture::LoadTexture(imagePath.c_str(), sRGB);
	}
	return id;
}

// Recupere seulement le premier mesh du premier node 
bool Mesh::ParseGLTF(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, Material& material, const char* filepath)
{
	std::string relativePath = "../data/";

	std::string relativeGLFTPath = relativePath + filepath;
	relativePath = relativeGLFTPath;

	simdjson::dom::parser parser;
	simdjson::dom::element gltf = parser.load(relativeGLFTPath);

	if (gltf.is_null())
		return false;

	relativePath.resize(relativeGLFTPath.rfind("/") + 1);

	{
		std::string_view version = gltf["asset"]["version"];

		int64_t nodeMeshId = -1;
		int64_t nodeMaterialId = -1;

		glm::mat4 transformMatrix = glm::mat4(1.f);

		for (const auto &node : gltf["nodes"])
		{
			int64_t meshId = 0;
			if (node["mesh"].is_uint64())
			{
				nodeMeshId = node["mesh"];

				glm::mat4 translationMatrix = glm::mat4(1.f);
				glm::mat4 rotationMatrix = glm::mat4(1.f);
				if (node["translation"].is_array()) {
					double tx = node["translation"].at(0);
					double ty = node["translation"].at(1);
					double tz = node["translation"].at(2);
					translationMatrix[3][0] = (float)tx;
					translationMatrix[3][1] = (float)ty;
					translationMatrix[3][2] = (float)tz;
				}
				if (node["rotation"].is_array()) {
					double qx = node["rotation"].at(0);
					double qy = node["rotation"].at(1);
					double qz = node["rotation"].at(2);
					double qw = node["rotation"].at(3);
					glm::quat q((float)qw, (float)qx, (float)qy, (float)qz);
					rotationMatrix = glm::toMat4(q);
				}
				transformMatrix = translationMatrix * rotationMatrix;
			}
		}

		std::vector<GLTFBuffer> buffers;

		for (const auto &buffer : gltf["buffers"]) 
		{
			GLTFBuffer geometryBuffer;
			size_t byteLength = buffer["byteLength"];
			std::string_view uri = buffer["uri"];
			geometryBuffer.data = new uint8_t[byteLength];
			std::string_view sub = uri.substr(uri.size() - 4, std::string_view::npos);
			if (uri.substr(uri.size() - 4, std::string_view::npos) != ".bin") {
				// todo
				return false;
			}
			else {
				std::string binPath = relativePath + uri.begin();
				FILE* bindata = fopen(binPath.c_str(), "rb");
				fread(geometryBuffer.data, byteLength, 1, bindata);
				fclose(bindata);
			}
			geometryBuffer.length = (uint32_t)byteLength;

			buffers.push_back(geometryBuffer);
		}

		// ---

		struct GltfSubMesh 
		{
			std::vector<float> positions;
			std::vector<float> normals;
			std::vector<float> uvs;
			std::vector<float> tangents;
			std::vector<uint16_t> indices16;
			uint32_t materialId;
		};

		std::vector<GltfSubMesh> submeshes;

		const auto &mesh = gltf["meshes"].at(nodeMeshId);
		{
			for (const auto primitive : mesh["primitives"])
			{
				//print_json(primitive);
				GltfSubMesh gltfSubMesh;

				if (primitive["indices"].is_int64()) {
					int64_t indicesId = primitive["indices"];
					ParseGLTFAttribute(gltf, buffers, (uint32_t)indicesId, gltfSubMesh.indices16);
				}
				int64_t positionId = primitive["attributes"]["POSITION"];
				ParseGLTFAttribute(gltf, buffers, (uint32_t)positionId, gltfSubMesh.positions);

				int64_t normalId = -1;
				if (primitive["attributes"]["NORMAL"].is_int64()) {
					normalId = primitive["attributes"]["NORMAL"];
					ParseGLTFAttribute(gltf, buffers, (uint32_t)normalId, gltfSubMesh.normals);
				}

				int64_t texcoords0Id = -1;
				if (primitive["attributes"]["TEXCOORD_0"].is_int64()) {
					texcoords0Id = primitive["attributes"]["TEXCOORD_0"];
					ParseGLTFAttribute(gltf, buffers, (uint32_t)texcoords0Id, gltfSubMesh.uvs);
				}

				int64_t tangentsId = -1;
				if (primitive["attributes"]["TANGENT"].is_int64()) {
					tangentsId = primitive["attributes"]["TANGENT"];
					ParseGLTFAttribute(gltf, buffers, (uint32_t)tangentsId, gltfSubMesh.tangents);
				}

				if (primitive["material"].is_uint64()) {
					nodeMaterialId = primitive["material"];
					gltfSubMesh.materialId = (uint32_t)nodeMaterialId + 1;
				}

				if (primitive["mode"].is_uint64()) {
					uint64_t mode = primitive["mode"];
					// 0: POINTS, 1: LINES, 2: _LOOP, 3: _STRIP, 4(d): TRIANGLES, 5: _STRIP, 6: _FAN
					if (mode != 4)
						__debugbreak();
				}

				submeshes.push_back(std::move(gltfSubMesh));
			}
		}

		{
			uint32_t submeshCount = 0;

			const auto& gltfSubMesh = submeshes[submeshCount];
			{
				// si tous les meshes sont triangularises c'est ok...
				int vertexCount = (uint32_t)gltfSubMesh.positions.size() / 3;
				// pour le moment on ne fait pas de deduplication, on stocke tout en brut
				//int indexCount = mesh.indices32.size();
				int indexCount = (uint32_t)gltfSubMesh.indices16.size();

				// buffer temporaire, on va tout stocker côté GPU par la suite			
				vertices.resize(vertexCount);
				indices.resize(indexCount);

				for (int i = 0; i < indexCount; i++) {
					//indices[i] = gltfSubMesh.indices32[i];
					indices[i] = gltfSubMesh.indices16[i];
				}

				for (int i = 0; i < vertexCount; i++) {
					vertices[i].position = glm::vec3(transformMatrix * glm::vec4{ gltfSubMesh.positions[i * 3 + 0], gltfSubMesh.positions[i * 3 + 1], gltfSubMesh.positions[i * 3 + 2], 1.f });
					vertices[i].normal = { 0.f, 0.f, 1.f };
					vertices[i].texcoords = { 0.f, 0.f };
					vertices[i].tangent = { 1.f, 0.f, 0.f, 1.f };
				}

				if (gltfSubMesh.uvs.size()) {
					for (int i = 0; i < vertexCount; i++)
						vertices[i].texcoords = { gltfSubMesh.uvs[i * 2 + 0], gltfSubMesh.uvs[i * 2 + 1] };
				}

				if (gltfSubMesh.normals.size())
				{
					for (int i = 0; i < vertexCount; i++)
						vertices[i].normal = glm::vec3(transformMatrix * glm::vec4{ gltfSubMesh.normals[i * 3 + 0], gltfSubMesh.normals[i * 3 + 1], gltfSubMesh.normals[i * 3 + 2], 0.f });
					if (gltfSubMesh.tangents.size()) {
						for (int i = 0; i < vertexCount; i++) {
							vertices[i].tangent = transformMatrix * glm::vec4{ gltfSubMesh.tangents[i * 2 + 0], gltfSubMesh.tangents[i * 2 + 1], gltfSubMesh.tangents[i * 2 + 2], 0.f };
							// handedness
							vertices[i].tangent.w = gltfSubMesh.tangents[i * 2 + 3];
						}
					}
				}
				else {
					//CalculateFaceNormals(vertices, vertexCount, indices, indexCount);
				}

				submeshCount++;
			}

			const auto& mat = gltf["materials"].at(nodeMaterialId);
			{
				material = Material::defaultMaterial;

				if (mat["pbrMetallicRoughness"].is_object())
				{
					material.diffuseTexture = ParseGLTFTexture(relativePath, gltf, mat["pbrMetallicRoughness"]["baseColorTexture"], true, Material::defaultMaterial.diffuseTexture);
					material.roughnessTexture = ParseGLTFTexture(relativePath, gltf, mat["pbrMetallicRoughness"]["metallicRoughnessTexture"], false, Material::defaultMaterial.roughnessTexture);

					if (mat["pbrMetallicRoughness"]["roughnessFactor"].is_double())
					{
						double factor = mat["pbrMetallicRoughness"]["roughnessFactor"];
						material.roughness = (float)factor;
					}
					if (mat["pbrMetallicRoughness"]["metallicFactor"].is_double())
					{
						double factor = mat["pbrMetallicRoughness"]["metallicFactor"];
						material.metalness = (float)factor;
					}
				}

				material.normalTexture = ParseGLTFTexture(relativePath, gltf, mat["normalTexture"], false, Material::defaultMaterial.normalTexture);
				material.ambientTexture = ParseGLTFTexture(relativePath, gltf, mat["occlusionTexture"], false, Material::defaultMaterial.ambientTexture);
				material.emissiveTexture = ParseGLTFTexture(relativePath, gltf, mat["emissiveTexture"], true, Material::defaultMaterial.emissiveTexture);

				if (mat["emissiveFactor"].is_array())
				{
					double r = mat["emissiveFactor"].at(0);
					double g = mat["emissiveFactor"].at(1);
					double b = mat["emissiveFactor"].at(2);
					material.emissiveColor = glm::vec3(r, g, b);
				}
			}
		}
	}

	return true;
}