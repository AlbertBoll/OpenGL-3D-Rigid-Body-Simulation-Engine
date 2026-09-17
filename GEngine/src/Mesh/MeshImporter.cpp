#include "gepch.h"
#include "Mesh/MeshImporter.h"
#include <assimp/cimport.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <limits>
#include <new>

namespace GEngine
{
    namespace
    {
        using Code = MeshImportErrorCode;
        using Field = MeshImportField;
        constexpr unsigned int ImportFlags=aiProcess_ValidateDataStructure | aiProcess_Triangulate;
        auto Error(Code code, std::size_t part=0, std::size_t element=0, Field field=Field::None)
        { return std::unexpected(MeshImportError{code,part,element,field}); }
        struct ReleaseScene { void operator()(const aiScene* scene) const noexcept { aiReleaseImport(scene); } };
        using SceneOwner = std::unique_ptr<const aiScene,ReleaseScene>;
        struct PartStorage
        {
            std::unique_ptr<MeshImportVector[]> positions, normals, tangents, bitangents;
            std::unique_ptr<MeshImportUV[]> uvs;
            std::unique_ptr<std::uint32_t[]> indices;
        };
        std::expected<void,MeshImportError> CopyVectors(const aiVector3D* input,
            std::size_t count, std::unique_ptr<MeshImportVector[]>& storage,
            std::span<const MeshImportVector>& view, std::size_t part)
        {
            if (!input) return {};
            storage.reset(new(std::nothrow) MeshImportVector[count]);
            if (!storage) return Error(Code::AllocationFailed,part);
            for (std::size_t i=0;i<count;++i) storage[i]={input[i].x,input[i].y,input[i].z};
            view={storage.get(),count};
            return {};
        }
        std::expected<void,MeshImportError> CopyPart(const aiMesh& mesh, const aiMatrix4x4& matrix,
            MeshImportPart& out, PartStorage& storage, std::size_t part)
        {
            if (mesh.mNumBones || mesh.mNumAnimMeshes) return Error(Code::UnsupportedContent,part);
            if (!mesh.mNumVertices || !mesh.mVertices || !mesh.mNumFaces || !mesh.mFaces) return Error(Code::EmptyMesh,part);
            if (mesh.mTextureCoords[0] && mesh.mNumUVComponents[0]!=2) return Error(Code::UnsupportedContent,part,0,Field::UV);
            const auto count=static_cast<std::size_t>(mesh.mNumVertices);
            const auto faces=static_cast<std::size_t>(mesh.mNumFaces);
            const auto limit=static_cast<std::size_t>((std::numeric_limits<std::ptrdiff_t>::max)());
            if (count>limit/sizeof(MeshImportVector) || faces>limit/(3*sizeof(std::uint32_t))) return Error(Code::SizeOverflow,part);
            const std::array<const aiVector3D*,4> inputs{mesh.mVertices,mesh.mNormals,mesh.mTangents,mesh.mBitangents};
            const std::array<std::unique_ptr<MeshImportVector[]>*,4> stores{&storage.positions,&storage.normals,&storage.tangents,&storage.bitangents};
            const std::array<std::span<const MeshImportVector>*,4> views{&out.positions,&out.normals,&out.tangents,&out.bitangents};
            for (std::size_t i=0;i<inputs.size();++i)
                if (auto copied=CopyVectors(inputs[i],count,*stores[i],*views[i],part); !copied) return copied;
            if (mesh.mTextureCoords[0])
            {
                storage.uvs.reset(new(std::nothrow) MeshImportUV[count]);
                if (!storage.uvs) return Error(Code::AllocationFailed,part);
                for (std::size_t i=0;i<count;++i) storage.uvs[i]={mesh.mTextureCoords[0][i].x,mesh.mTextureCoords[0][i].y};
                out.uvs={storage.uvs.get(),count};
            }
            storage.indices.reset(new(std::nothrow) std::uint32_t[faces*3]);
            if (!storage.indices) return Error(Code::AllocationFailed,part);
            for (std::size_t f=0;f<faces;++f)
            {
                const auto& face=mesh.mFaces[f];
                if (face.mNumIndices!=3 || !face.mIndices) return Error(Code::InvalidTriangleRange,part,f,Field::Index);
                for (std::size_t j=0;j<3;++j) storage.indices[f*3+j]=face.mIndices[j];
            }
            out.indices={storage.indices.get(),faces*3};
            out.materialSlot=mesh.mMaterialIndex;
            out.transform={matrix.a1,matrix.a2,matrix.a3,matrix.a4, matrix.b1,matrix.b2,matrix.b3,matrix.b4,
                matrix.c1,matrix.c2,matrix.c3,matrix.c4, matrix.d1,matrix.d2,matrix.d3,matrix.d4};
            return {};
        }
        // A counting pass and a copying pass use identical traversal order. The
        // depth bound rejects malformed/cyclic or unsupported very deep graphs.
        std::expected<void,MeshImportError> Visit(const aiScene& scene, const aiNode* node,
            const aiMatrix4x4& parent, std::size_t depth, std::size_t& ordinal,
            std::span<MeshImportPart> parts={}, std::span<PartStorage> storage={})
        {
            if (!node || depth>256 || (node->mNumMeshes && !node->mMeshes) || (node->mNumChildren && !node->mChildren))
                return Error(Code::InvalidScene,ordinal,depth);
            const auto transform=parent*node->mTransformation;
            for (std::size_t i=0;i<node->mNumMeshes;++i)
            {
                const auto index=node->mMeshes[i];
                if (index>=scene.mNumMeshes || !scene.mMeshes[index]) return Error(Code::InvalidScene,ordinal,index);
                if (ordinal==UINT32_MAX) return Error(Code::SizeOverflow,ordinal);
                if (!parts.empty())
                {
                    if (ordinal>=parts.size()) return Error(Code::InvalidScene,ordinal);
                    auto copied=CopyPart(*scene.mMeshes[index],transform,parts[ordinal],storage[ordinal],ordinal);
                    if (!copied) return copied;
                }
                ++ordinal;
            }
            for (std::size_t i=0;i<node->mNumChildren;++i)
                if (auto result=Visit(scene,node->mChildren[i],transform,depth+1,ordinal,parts,storage); !result) return result;
            return {};
        }
        std::expected<MeshAsset,MeshImportError> Convert(SceneOwner scene, const MeshImportOptions& options)
        {
            // C import entry points own third-party error handling. Diagnostics
            // here are stable engine codes/ordinals, not global Assimp strings.
            if (!scene) return Error(Code::ReadFailed);
            if ((scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode || !scene->mNumMeshes || !scene->mMeshes
                || !scene->mNumMaterials || !scene->mMaterials) return Error(Code::InvalidScene);
            if (scene->mNumAnimations) return Error(Code::UnsupportedContent);
            std::size_t count=0;
            if (auto counted=Visit(*scene,scene->mRootNode,aiMatrix4x4{},0,count); !counted) return std::unexpected(counted.error());
            if (!count) return Error(Code::EmptyMesh);
            const auto limit=static_cast<std::size_t>((std::numeric_limits<std::ptrdiff_t>::max)());
            if (count>limit/sizeof(MeshImportPart) || count>limit/sizeof(PartStorage)) return Error(Code::SizeOverflow);
            std::unique_ptr<MeshImportPart[]> parts(new(std::nothrow) MeshImportPart[count]);
            std::unique_ptr<PartStorage[]> storage(new(std::nothrow) PartStorage[count]);
            if (!parts || !storage) return Error(Code::AllocationFailed);
            std::size_t ordinal=0;
            if (auto copied=Visit(*scene,scene->mRootNode,aiMatrix4x4{},0,ordinal,{parts.get(),count},{storage.get(),count}); !copied)
                return std::unexpected(copied.error());
            return NormalizeImportedMesh({parts.get(),count},scene->mNumMaterials,options);
        }
    }

    std::expected<MeshAsset,MeshImportError> ImportMeshFile(const char* path, const MeshImportOptions& options)
    {
        if (!path || !*path) return Error(Code::InvalidPath);
        return Convert(SceneOwner(aiImportFile(path,ImportFlags)),options);
    }
    std::expected<MeshAsset,MeshImportError> ImportMeshMemory(std::span<const std::byte> bytes,
        const char* formatHint, const MeshImportOptions& options)
    {
        if (bytes.empty()) return Error(Code::ReadFailed);
        if (bytes.size()>UINT32_MAX) return Error(Code::SizeOverflow);
        return Convert(SceneOwner(aiImportFileFromMemory(reinterpret_cast<const char*>(bytes.data()),
            static_cast<unsigned int>(bytes.size()),ImportFlags,formatHint ? formatHint : "")),options);
    }
}
