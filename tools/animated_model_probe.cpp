#ifdef ANIMATED_MODEL_SCHEMA_ONLY
#include "Animation/AnimatedModel.h"
#include "Managers/ShapeManager.h"
#include <type_traits>
static_assert(!std::is_copy_constructible_v<::GEngine::AnimatedModel>);
static_assert(std::is_move_constructible_v<::GEngine::AnimatedModel>);
static_assert(std::is_same_v<decltype(::GEngine::AnimatedModel::Create(std::declval<const std::string&>())),
    std::expected<::GEngine::AnimatedModel,::GEngine::ModelImportError>>);
static_assert(std::is_same_v<::GEngine::Manager::ShapeManager::ModelsResult,
    std::expected<std::reference_wrapper<const std::vector<::GEngine::Geometry*>>,::GEngine::Manager::ShapeManager::ModelError>>);
#else
#define main ExistingShutdownMain
#include "shutdown_probe.cpp"
#undef main
#include "Animation/AnimatedModel.h"
#include "../GEngine/src/Geometry/GeometryBackend.h"
#ifdef ANIMATED_MODEL_INTERNAL_PROBE
#include "../GEngine/src/Animation/AnimatedModel.cpp"
namespace
{
    void OrderedMeshes(const aiNode& node,const aiScene& scene,std::vector<const aiMesh*>& meshes)
    {
        for(unsigned i=0;i<node.mNumMeshes;++i)meshes.push_back(scene.mMeshes[node.mMeshes[i]]);
        for(unsigned i=0;i<node.mNumChildren;++i)OrderedMeshes(*node.mChildren[i],scene,meshes);
    }
    template<class T> const std::vector<T>& Payload(Geometry& geometry,unsigned slot)
    {
        auto& attribute=std::get<Buffer::Attribute<T>>(geometry.GetAttributes().at(slot));
        const auto& cpu=attribute.m_Data;
        geometry.BindVAO();attribute.AssociateSlot(slot);
        glBindBuffer(GL_ARRAY_BUFFER,::GEngine::GeometryDetail::BackendAccess::AttributeBuffer(attribute));
        GLint bytes=0;glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&bytes);
        Check(bytes==cpu.size()*sizeof(T),"GPU attribute byte size changed");
        std::vector<T> gpu(cpu.size());
        if(bytes)glGetBufferSubData(GL_ARRAY_BUFFER,0,bytes,gpu.data());
        Check(gpu==cpu,"CPU/GPU animated attribute payload differs");
        return cpu;
    }
    void AnimatedModels()
    {
        auto outside=ShapeManager::GetModels("dancing_vampire");
        Check(!outside && std::get<PlatformError>(outside.error()).code==PlatformErrorCode::InvalidState,"Outside-root lookup not typed");
        RuntimeAssets::Initialize("GEngineEditor");
        auto app=std::make_unique<BaseApp>();Check(app->Initialize(Properties()).has_value(),"Model test initialization failed");
        ModelNames names;
        const auto path=RuntimeAssets::File("AnimatedModels/dancing_vampire.dae");
        Assimp::Importer importer;
        auto* scene=const_cast<aiScene*>(importer.ReadFile(path,aiProcess_Triangulate|aiProcess_GenSmoothNormals|aiProcess_CalcTangentSpace));
        Check(scene && scene->mRootNode,"Native reference import failed");
        std::vector<const aiMesh*> ordered;OrderedMeshes(*scene->mRootNode,*scene,ordered);
        unsigned boneVertices=0;
        {
            auto model=AnimatedModel::Create(path);Check(model.has_value(),"Valid animated import failed");
            auto moved=std::move(*model);
            auto replacement=AnimatedModel::Create(path);Check(replacement.has_value(),"Move assignment target import failed");
            *replacement=std::move(moved);
            auto meshes=std::move(*replacement).TakeGeometries();
            Check(meshes.size()==ordered.size() && !meshes.empty(),"Traversal or ownership transfer changed");
            Check(ModelNames::buffers.size()==8*meshes.size() && ModelNames::arrays.size()==meshes.size(),"Move assignment did not retire previous model exactly");
            std::unordered_map<std::string,int> ids;int nextId=0;
            for(size_t n=0;n<meshes.size();++n)
            {
                const auto& native=*ordered[n];auto& geometry=*meshes[n];
                const auto& position=Payload<Vec3f>(geometry,0);const auto& uv=Payload<Vec2f>(geometry,1);
                const auto& normal=Payload<Vec3f>(geometry,2);const auto& tangent=Payload<Vec3f>(geometry,3);
                const auto& bitangent=Payload<Vec3f>(geometry,4);const auto& bones=Payload<Vec4i>(geometry,5);
                const auto& weights=Payload<Vec4f>(geometry,6);
                Check(position.size()==native.mNumVertices && bones.size()==position.size() && weights.size()==position.size(),"Vertex/bone count changed");
                for(unsigned v=0;v<native.mNumVertices;++v)
                {
                    auto vec=[](const aiVector3D& a){return Vec3f(a.x,a.y,a.z);};
                    Check(position[v]==vec(native.mVertices[v]) && normal[v]==vec(native.mNormals[v]),"Position/normal changed");
                    if(native.mTextureCoords[0])Check(uv[v]==Vec2f(native.mTextureCoords[0][v].x,native.mTextureCoords[0][v].y)
                        && tangent[v]==vec(native.mTangents[v]) && bitangent[v]==vec(native.mBitangents[v]),"UV/tangent/bitangent changed");
                }
                std::vector<Vec4i> expectedIds(native.mNumVertices,Vec4i(-1));
                std::vector<Vec4f> expectedWeights(native.mNumVertices,Vec4f(0));
                for(unsigned b=0;b<native.mNumBones;++b)
                {
                    const auto& bone=*native.mBones[b];const std::string name=bone.mName.C_Str();
                    if(!ids.contains(name))ids.emplace(name,nextId++);
                    const auto& info=replacement->GetBoneInfoMap().at(name);
                    Check(info.id==ids.at(name) && info.offset==AssimpGLMHelpers::ConvertMatrixToGLMFormat(bone.mOffsetMatrix),"Bone identifier/offset changed");
                    for(unsigned w=0;w<bone.mNumWeights;++w)
                    {
                        const auto& weight=bone.mWeights[w];
                        for(unsigned slot=0;slot<4;++slot)if(expectedIds[weight.mVertexId][slot]<0)
                        {expectedIds[weight.mVertexId][slot]=info.id;expectedWeights[weight.mVertexId][slot]=weight.mWeight;break;}
                    }
                }
                Check(bones==expectedIds && weights==expectedWeights,"Bone influence order/weights changed");
                boneVertices+=native.mNumVertices;
                unsigned indices=0;for(unsigned f=0;f<native.mNumFaces;++f)indices+=native.mFaces[f].mNumIndices;
                Check(geometry.GetIndicesCount()==indices,"Index count changed");
            }
            Check(replacement->GetBoneCount()==nextId && replacement->GetBoneInfoMap().size()==ids.size() && nextId>0,"Bone map cardinality changed");
        }
        names.Empty();
        auto missing=AnimatedModel::Create("phase66-missing.dae");
        Check(!missing && missing.error().code==ModelImportErrorCode::ImportFailed && missing.error().operation=="AnimatedModel::Create"
            && missing.error().source=="phase66-missing.dae" && missing.error().message.starts_with("ERROR::ASSIMP:: ")
            && missing.error().message.size()>16,"Missing import lost diagnostic");
        {std::ofstream file("malformed.dae");file<<"<COLLADA invalid";file.close();Check(!file.fail(),"Fixture write failed");}
        auto malformed=AnimatedModel::Create("malformed.dae");Check(!malformed && !malformed.error().message.empty(),"Malformed model not recoverable");
        {
            auto* root=scene->mRootNode;
            struct Restore {aiScene* scene;aiNode* root;unsigned meshes,children;
                ~Restore(){scene->mRootNode=root;root->mNumMeshes=meshes;root->mNumChildren=children;}}
                restore{scene,root,root->mNumMeshes,root->mNumChildren};
            scene->mRootNode=nullptr;auto invalid=::GEngine::BuildAnimatedModel(*scene,"invalid-root");
            Check(!invalid && invalid.error().code==ModelImportErrorCode::InvalidScene,"Invalid root not typed");
            scene->mRootNode=root;root->mNumMeshes=root->mNumChildren=0;
            auto empty=::GEngine::BuildAnimatedModel(*scene,"empty-model");
            Check(!empty && empty.error().code==ModelImportErrorCode::NoGeometry && empty.error().message=="Model contains no geometry: empty-model","Empty model not typed");
        }
        {
            aiMesh* mesh=nullptr;aiVertexWeight* weight=nullptr;
            for(unsigned m=0;m<scene->mNumMeshes && !weight;++m)
                for(unsigned b=0;b<scene->mMeshes[m]->mNumBones && !weight;++b)
                    if(scene->mMeshes[m]->mBones[b]->mNumWeights){mesh=scene->mMeshes[m];weight=&mesh->mBones[b]->mWeights[0];}
            Check(weight!=nullptr,"Missing bone weight fixture");
            struct Restore {aiVertexWeight* weight;unsigned id;~Restore(){weight->mVertexId=id;}}restore{weight,weight->mVertexId};
            weight->mVertexId=mesh->mNumVertices;
            const auto arrays=ModelNames::generatedArrays;
            ::GEngine::AnimatedImportState state;
            auto invalid=::GEngine::ReadAnimatedMesh(*mesh,state,"invalid-weight");
            Check(!invalid && invalid.error().code==ModelImportErrorCode::InvalidData && invalid.error().source=="invalid-weight"
                && invalid.error().message.find("vertex="+std::to_string(mesh->mNumVertices))!=std::string::npos,"Endpoint bone weight not typed");
            Check(ModelNames::generatedArrays==arrays+1,"Weight fixture did not exercise partial geometry construction");
            names.Empty();
        }
        {
            std::ofstream file("two-meshes.obj");
            file<<"o first\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nf 1/1 2/2 3/3\n"
                "o second\nv 0 0 2\nv 1 0 2\nv 0 1 2\nf 4/1 5/2 6/3\n";
            file.close();Check(!file.fail(),"Partial fixture write failed");
            Assimp::Importer partialImporter;
            auto* partialScene=const_cast<aiScene*>(partialImporter.ReadFile("two-meshes.obj",aiProcess_Triangulate|aiProcess_GenSmoothNormals|aiProcess_CalcTangentSpace));
            Check(partialScene && partialScene->mNumMeshes==2,"Partial native fixture failed");
            auto& index=partialScene->mMeshes[1]->mFaces[0].mIndices[0];
            struct Restore {unsigned& index;unsigned value;~Restore(){index=value;}}restore{index,index};
            index=partialScene->mMeshes[1]->mNumVertices;
            const auto made=ModelNames::generatedBuffers;
            auto partial=::GEngine::BuildAnimatedModel(*partialScene,"partial-model");
            Check(!partial && partial.error().code==ModelImportErrorCode::InvalidData && ModelNames::generatedBuffers==made+8,"Prior mesh was not built before partial failure");
            names.Empty();
        }
        auto first=ShapeManager::GetModels("dancing_vampire");Check(first && !first->get().empty(),"Manager import failed");
        const auto made=ModelNames::generatedBuffers;const auto* list=&first->get();const auto* firstMesh=first->get().front();
        auto again=ShapeManager::GetModels("dancing_vampire");
        Check(again && &again->get()==list && again->get().front()==firstMesh && ModelNames::generatedBuffers==made,"Cache borrower changed");
        for(int repeat=0;repeat<2;++repeat){auto absent=ShapeManager::GetModels("phase66-missing-model");
            Check(!absent && std::holds_alternative<PlatformError>(absent.error()) && ModelNames::generatedBuffers==made,"Missing manager input was cached/allocated");}
        Check(ShapeManager::RetireShape("dancing_vampire").has_value(), "Typed shape retirement failed");
        Check(firstMesh->GetVerticesCount()>0 && &first->get()==list,"Unregister invalidated retained borrower");
        auto fresh=ShapeManager::GetModels("dancing_vampire");Check(fresh && &fresh->get()==list && fresh->get().front()==firstMesh && ModelNames::generatedBuffers==made,"Shape unregister changed animated-model cache");
        app.reset();names.Empty();PlatformGone();
        Check(!ShapeManager::GetModels("dancing_vampire"),"Manager accessible after teardown");
        std::cout<<"[PASS] animated model payload/bones/errors/moves/partial/cache ownership vertices="<<boneVertices
            <<" buffers="<<ModelNames::generatedBuffers<<"/"<<ModelNames::retiredBuffers
            <<" arrays="<<ModelNames::generatedArrays<<"/"<<ModelNames::retiredArrays<<" checks="<<checks<<'\n';
    }
}
int main(){SDL_SetMainReady();try{AnimatedModels();return 0;}catch(const std::exception& e){std::cerr<<"[FAIL] "<<e.what()<<'\n';return 1;}}
#else
#define main ProductionEntryPoint
#include "EntryPoint.h"
#undef main
namespace AnimatedStartup
{
    bool fail=false;unsigned runs=0,renders=0,destructors=0;
    std::unique_ptr<ModelNames> names;
    class App final:public ::GEngine::BaseApp
    {
    public:
        ::GEngine::ApplicationInitializationResult Initialize(const std::initializer_list<::GEngine::WindowProperties>& props)override
        {
            auto initialized=BaseApp::Initialize(props);if(!initialized)return initialized;
            auto model=ShapeManager::GetModels(fail?"phase66-missing-model":"dancing_vampire");
            if(!model)return std::visit([](const auto& error)->::GEngine::ApplicationInitializationResult{
                return std::unexpected(::GEngine::ApplicationInitializationError{error});},model.error());
            return {};
        }
        ::GEngine::ApplicationRunResult Run()override{++runs;Render();return {};}
        void Render()override{++renders;}
        ~App()override{++destructors;Check(SDL_GL_GetCurrentContext()!=nullptr,"App outlived platform");}
    };
}
extern "C" int Phase66RealGladLoadGL(void);
extern "C" int gladLoadGL(void){const int result=Phase66RealGladLoadGL();if(result)AnimatedStartup::names=std::make_unique<ModelNames>();return result;}
::GEngine::WindowProperties winProp=Properties();
::GEngine::BaseApp* CreateApp(){return new AnimatedStartup::App;}
int main(int argc,char** argv)
{
    if(argc!=2)return 2;const std::string_view mode=argv[1];if(mode!="failure" && mode!="success")return 2;
    AnimatedStartup::fail=mode=="failure";SDL_SetMainReady();const int status=ProductionEntryPoint(argc,argv);
    Check(status==(AnimatedStartup::fail?1:0) && AnimatedStartup::runs==(AnimatedStartup::fail?0u:1u)
        && AnimatedStartup::renders==AnimatedStartup::runs && AnimatedStartup::destructors==1,"Startup exit/Run/render/destruction failed");
    Check(AnimatedStartup::names!=nullptr,"Actual startup not reached");
    AnimatedStartup::names->Empty();AnimatedStartup::names.reset();PlatformGone();
    std::cout<<"[PASS] animated model startup "<<mode<<" exit="<<status<<" runs="<<AnimatedStartup::runs<<" renders="<<AnimatedStartup::renders
        <<" buffers="<<ModelNames::generatedBuffers<<"/"<<ModelNames::retiredBuffers
        <<" arrays="<<ModelNames::generatedArrays<<"/"<<ModelNames::retiredArrays<<" root=0 SDL=0 TTF=0\n";
    return status;
}
#endif
#endif
