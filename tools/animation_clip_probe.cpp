#ifdef ANIMATION_CLIP_SCHEMA_ONLY
#include "Animation/Animation.h"
#include "Animation/AnimatedModel.h"
#include <type_traits>
static_assert(std::is_same_v<decltype(::GEngine::Animation::Create(std::declval<const std::string&>(),std::declval<::GEngine::AnimatedModel&>())),
    std::expected<std::unique_ptr<::GEngine::Animation>,::GEngine::ModelImportError>>);
#else
#define main ExistingShutdownMain
#include "shutdown_probe.cpp"
#undef main
#include "Animation/Animation.h"
#include "Animation/AnimatedModel.h"
#include "Animation/AnimationSystem.h"
#ifdef ANIMATION_CLIP_INTERNAL_PROBE
#include "../GEngine/src/Animation/Animation.cpp"
namespace
{
    bool MapsEqual(const std::unordered_map<std::string,BoneInfo>& a,const std::unordered_map<std::string,BoneInfo>& b)
    {
        if(a.size()!=b.size())return false;
        for(const auto& [name,bone]:a){auto found=b.find(name);if(found==b.end() || found->second.id!=bone.id || found->second.offset!=bone.offset)return false;}
        return true;
    }
    bool Near(const Mat4& a,const Mat4& b)
    {for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(std::abs(a[c][r]-b[c][r])>2e-5f)return false;return true;}
    unsigned Hierarchy(const AssimpNodeData& value,const aiNode& native)
    {
        Check(value.name==native.mName.C_Str() && value.transformation==AssimpGLMHelpers::ConvertMatrixToGLMFormat(native.mTransformation)
            && value.childrenCount==native.mNumChildren && value.children.size()==native.mNumChildren,"Clip hierarchy payload changed");
        unsigned count=1;for(unsigned i=0;i<native.mNumChildren;++i)count+=Hierarchy(value.children[i],*native.mChildren[i]);return count;
    }
    void Clips()
    {
        RuntimeAssets::Initialize("GEngineEditor");auto app=std::make_unique<BaseApp>();
        Check(app->Initialize(Properties()).has_value(),"Clip fixture initialization failed");ModelNames names;
        unsigned channels=0,nodes=0;
        {
            const auto path=RuntimeAssets::File("AnimatedModels/dancing_vampire.dae");
            auto model=AnimatedModel::Create(path);Check(model.has_value(),"Model prerequisite failed");
            const auto before=model->GetBoneInfoMap();const auto count=model->GetBoneCount();
            auto unchanged=[&]{Check(count==model->GetBoneCount() && MapsEqual(before,model->GetBoneInfoMap()),"Failed clip changed model metadata");};
            auto missing=Animation::Create("phase66-missing-clip.dae",*model);
            Check(!missing && missing.error().code==ModelImportErrorCode::ImportFailed && missing.error().source=="phase66-missing-clip.dae"
                && missing.error().operation=="Animation::Create" && missing.error().message.starts_with("ERROR::ASSIMP:: ")
                && missing.error().message.size()>16,"Missing clip lost complete typed diagnostic");unchanged();
            {std::ofstream file("malformed.dae");file<<"<COLLADA invalid";file.close();Check(!file.fail(),"Malformed fixture write failed");}
            auto malformed=Animation::Create("malformed.dae",*model);Check(!malformed && !malformed.error().message.empty(),"Malformed clip did not fail recoverably");unchanged();
            {std::ofstream file("no-animation.obj");file<<"v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";file.close();Check(!file.fail(),"No-clip fixture write failed");}
            auto noClip=Animation::Create("no-animation.obj",*model);
            Check(!noClip && noClip.error().code==ModelImportErrorCode::InvalidScene && noClip.error().message=="Model contains no animation clip: no-animation.obj","No-clip scene not typed");unchanged();
            Assimp::Importer importer;auto* scene=const_cast<aiScene*>(importer.ReadFile(path,aiProcess_Triangulate));
            Check(scene && scene->mRootNode && scene->mNumAnimations && scene->mAnimations[0]->mNumChannels>1,"Native clip fixture invalid");
            auto* native=scene->mAnimations[0];
            {
                auto* root=scene->mRootNode;struct Restore {aiScene* s;aiNode* root;~Restore(){s->mRootNode=root;}}restore{scene,root};
                scene->mRootNode=nullptr;auto invalid=::GEngine::BuildAnimation(*scene,*model,"invalid-root");
                Check(!invalid && invalid.error().code==ModelImportErrorCode::InvalidScene,"Missing root not typed");unchanged();
                auto child=::GEngine::ReadClipHierarchy(nullptr,"invalid-child");Check(!child && child.error().code==ModelImportErrorCode::InvalidData,"Missing hierarchy child not typed");
            }
            {
                auto** saved=native->mChannels;struct Restore {aiAnimation* clip;aiNodeAnim** data;~Restore(){clip->mChannels=data;}}restore{native,saved};
                native->mChannels=nullptr;auto invalid=::GEngine::BuildAnimation(*scene,*model,"invalid-channels");
                Check(!invalid && invalid.error().code==ModelImportErrorCode::InvalidData,"Missing channels not typed");unchanged();
            }
            {
                auto*& channel=native->mChannels[1];auto* saved=channel;
                struct Restore {aiNodeAnim*& channel;aiNodeAnim* saved;~Restore(){channel=saved;}}restore{channel,saved};
                channel=nullptr;auto invalid=::GEngine::BuildAnimation(*scene,*model,"partial-channels");
                Check(!invalid && invalid.error().message=="Animation channel has incomplete key data: channel=1","Partial channel diagnostic lost");unchanged();
            }
            {
                auto* channel=native->mChannels[0];Check(channel->mNumPositionKeys && channel->mNumRotationKeys && channel->mNumScalingKeys,"Key fixture absent");
                struct Restore {aiNodeAnim* channel;aiVectorKey* position;aiQuatKey* rotation;aiVectorKey* scale;
                    ~Restore(){channel->mPositionKeys=position;channel->mRotationKeys=rotation;channel->mScalingKeys=scale;}}
                    restore{channel,channel->mPositionKeys,channel->mRotationKeys,channel->mScalingKeys};
                for(int track=0;track<3;++track)
                {
                    channel->mPositionKeys=track==0?nullptr:restore.position;channel->mRotationKeys=track==1?nullptr:restore.rotation;
                    channel->mScalingKeys=track==2?nullptr:restore.scale;
                    auto invalid=::GEngine::BuildAnimation(*scene,*model,"invalid-keys");
                    Check(!invalid && invalid.error().code==ModelImportErrorCode::InvalidData && invalid.error().source=="invalid-keys","Missing key array not typed");unchanged();
                }
            }
            auto clip=Animation::Create(path,*model);Check(clip.has_value(),"Valid clip import failed");
            Check((*clip)->GetDuration()==float(native->mDuration) && (*clip)->GetTicksPerSecond()==float(native->mTicksPerSecond),"Duration/ticks changed");
            nodes=Hierarchy((*clip)->GetRootNode(),*scene->mRootNode);channels=native->mNumChannels;
            int expectedCount=count;auto expected=before;
            for(unsigned i=0;i<native->mNumChannels;++i)
            {
                const auto& channel=*native->mChannels[i];const std::string name=channel.mNodeName.C_Str();
                if(!expected.contains(name))expected[name].id=expectedCount++;
                auto* bone=(*clip)->FindBone(name);Check(bone && bone->GetBoneID()==expected.at(name).id,"Channel identity or missing-bone assignment changed");
                Check(channel.mPositionKeys[0].mTime==0 && channel.mRotationKeys[0].mTime==0 && channel.mScalingKeys[0].mTime==0,"Reference first-key time changed");
                Check(bone->Update(0).has_value(),"First-key sampling failed");
                const auto pos=AssimpGLMHelpers::GetGLMVec(channel.mPositionKeys[0].mValue);
                const auto rot=glm::normalize(AssimpGLMHelpers::GetGLMQuat(channel.mRotationKeys[0].mValue));
                const auto scale=AssimpGLMHelpers::GetGLMVec(channel.mScalingKeys[0].mValue);
                Check(Near(bone->GetLocalTransform(),glm::translate(Mat4(1),pos)*glm::toMat4(rot)*glm::scale(Mat4(1),scale)),"First-key position/rotation/scale payload changed");
            }
            Check(model->GetBoneCount()==expectedCount && MapsEqual(expected,model->GetBoneInfoMap())
                && MapsEqual(expected,(*clip)->GetBoneIDMap()),"Successful model/clip metadata commit changed");
            Check(!(*clip)->FindBone("phase66-absent-bone"),"Absent-bone lookup behavior changed");
            AnimationSystem system(clip->release());Check(system.UpdateAnimation(0).has_value(),"Imported clip ownership/sample handoff failed");
        }
        names.Empty();app.reset();PlatformGone();
        std::cout<<"[PASS] animation clip payload/hierarchy/bones/typed-errors/transactional-metadata/ownership channels="<<channels<<" nodes="<<nodes
            <<" buffers="<<ModelNames::generatedBuffers<<"/"<<ModelNames::retiredBuffers<<" arrays="<<ModelNames::generatedArrays<<"/"<<ModelNames::retiredArrays<<'\n';
    }
}
int main(){SDL_SetMainReady();try{Clips();return 0;}catch(const std::exception& e){std::cerr<<"[FAIL] "<<e.what()<<'\n';return 1;}}
#else
#include "SceneApp.h"
#define main ProductionEntryPoint
#include "EntryPoint.h"
#undef main
namespace ClipCaller {bool fail=false;unsigned runs=0,renders=0,destructors=0,calls=0;std::unique_ptr<ModelNames> names;}
extern "C" int Phase66RealGladLoadGL(void);
extern "C" int gladLoadGL(void)
{const int result=Phase66RealGladLoadGL();if(result)ClipCaller::names=std::make_unique<ModelNames>();return result;}
struct ClipImportFactory
{
    static auto Create(const std::string& path,::GEngine::AnimatedModel& model)
    {++ClipCaller::calls;return ::GEngine::Animation::Create(ClipCaller::fail?"phase66-missing-clip.dae":path,model);}
};
#define Animation ClipImportFactory
#define CreateApp OriginalCreateApp
#include "../GEngineEditor/src/SceneApp.cpp"
#undef CreateApp
#undef Animation
namespace ClipCaller
{
    class App final:public SceneApp
    {
    public:
        ::GEngine::ApplicationRunResult Run()override{++runs;Render();return {};}
        void Render()override{++renders;}
        ~App()override{++destructors;}
    };
}
::GEngine::WindowProperties winProp=[] {auto p=Properties();p.m_Width=800;p.m_Height=600;return p;}();
::GEngine::BaseApp* CreateApp(){return new ClipCaller::App;}
int main(int argc,char** argv)
{
    if(argc!=2)return 2;const std::string_view mode=argv[1];if(mode!="failure" && mode!="success")return 2;
    ClipCaller::fail=mode=="failure";SDL_SetMainReady();
    std::set_terminate([]{std::cerr<<"[FAIL] actual Editor unexpected terminate calls="<<ClipCaller::calls<<" runs="<<ClipCaller::runs
        <<" renders="<<ClipCaller::renders<<" derived-destructors="<<ClipCaller::destructors<<'\n';std::_Exit(98);});
    const int result=ProductionEntryPoint(argc,argv);
    Check(result==(ClipCaller::fail?1:0) && ClipCaller::calls==1 && ClipCaller::runs==(ClipCaller::fail?0u:1u)
        && ClipCaller::renders==ClipCaller::runs && ClipCaller::destructors==1,"Actual Editor startup exit/Run/render contract failed");
    Check(ClipCaller::names!=nullptr,"Loader ownership observer not installed");
    ClipCaller::names->Empty();ClipCaller::names.reset();
    PlatformGone();Check(!::GEngine::EngineContext::TryGet(),"Root survived Editor shutdown");
    std::cout<<"[PASS] Editor exact ownership buffers="<<ModelNames::generatedBuffers<<"/"<<ModelNames::retiredBuffers
        <<" arrays="<<ModelNames::generatedArrays<<"/"<<ModelNames::retiredArrays<<" owner-context/thread=1\n";
    std::cout<<"[PASS] animation clip Editor "<<mode<<" exit="<<result<<" runs="<<ClipCaller::runs<<" renders="<<ClipCaller::renders<<" root=0 SDL=0 TTF=0\n";
    return result;
}
#endif
#endif
