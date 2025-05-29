#pragma once
#include<vector>
#include<algorithm>

namespace GEngine
{


    template <typename Signature>
    class Delegate;

    /**** namespace scope swap function ****/
    template <typename Signature>
    void swap(Delegate<Signature>& d1, Delegate<Signature>& d2)
    {
        d1.Swap(d2);
    }

    /**** delegate partial class template for function types ****/
    template <typename Ret, typename... Args>
    class Delegate<Ret(Args...)>
    {
       inline static int currentTotalID = 0;
    public:
        Delegate();

        Delegate(Delegate const& other);

        Delegate(Delegate&& other);

        ~Delegate();

        Delegate& operator=(Delegate const& other);

        Delegate& operator=(Delegate&& other);

        template <Ret(*FreeFunction)(Args...)>
        void Bind();

        template <typename Type, Ret(Type::* PtrToMemFun)(Args...)>
        void Bind(Type& instance);

        template <typename Type, Ret(Type::* PtrToConstMemFun)(Args...) const>
        void Bind(Type& instance);

        template <typename Type>
        void Bind(Type& funObj);

        template <typename Type>
        void Bind(Type&& funObj);

        void Swap(Delegate& other);

        explicit operator bool() const { return mFunction != nullptr; }

        Ret operator()(Args... args) { return mFunction(&mData, std::forward<Args>(args)...); }
        Ret Invoke(Args... args) { return mFunction(&mData, std::forward<Args>(args)...); }

        int GetID() const { return mID; }

    private:
        //typedef typename std::aligned_storage<sizeof(void*), alignof(void*)>::type Storage;
        using Storage = std::aligned_storage_t<sizeof(void*), alignof(void*)>;
        using Function = Ret(*)(void*, Args...);

        Storage mData;
        Function mFunction;

        bool mStored = false;
        int mID = 0;

        using DestroyStorageFunction = void(*)(Delegate*);
        using CopyStorageFunction = void(*)(const Delegate*, Delegate*);
        using MoveStorageFunction = void(*)(Delegate*, Delegate*);

        DestroyStorageFunction mDestroyStorage = nullptr;
        CopyStorageFunction mCopyStorage = nullptr;
        MoveStorageFunction mMoveStorage = nullptr;

        /**** helper function templates for special member functions ****/
        template <typename Type>
        static void DestroyStorage(Delegate* delegate)
        {
            reinterpret_cast<Type*>(&delegate->mData)->~Type();
        }

        template <typename Type>
        static void CopyStorage(const Delegate* src, Delegate* dst)
        {
            new(&dst->mData) Type(*reinterpret_cast<const Type*>(&src->mData));
        }

        template <typename Type>
        static void MoveStorage(Delegate* src, Delegate* dst)
        {
            new(&dst->mData) Type(std::move(*reinterpret_cast<Type*>(&src->mData)));
        }

        /**** stub functions ****/
        template <Ret(*FreeFunction)(Args...)>
        static Ret Stub(void* data, Args... args)
        {
            return FreeFunction(std::forward<Args>(args)...);
        }

        template <typename Type, Ret(Type::* PtrToMemFun)(Args...)>
        static Ret Stub(void* data, Args... args)
        {
            Storage* storage = static_cast<Storage*>(data);
            Type* instance = *reinterpret_cast<Type**>(storage);

            return (instance->*PtrToMemFun)(std::forward<Args>(args)...);
        }

        template <typename Type, Ret(Type::* PtrToConstMemFun)(Args...) const>
        static Ret Stub(void* data, Args... args)
        {
            Storage* storage = static_cast<Storage*>(data);
            Type* instance = *reinterpret_cast<Type**>(storage);

            return (instance->*PtrToConstMemFun)(std::forward<Args>(args)...);
        }

        template <typename Type>
        static Ret Stub(void* data, Args... args)
        {
            Storage* storage = static_cast<Storage*>(data);
            Type* instance = *reinterpret_cast<Type**>(storage);

            return (*instance)(std::forward<Args>(args)...);
        }

        template <typename Type, typename>
        static Ret Stub(void* data, Args... args)
        {
            Storage* storage = static_cast<Storage*>(data);
            Type* instance = reinterpret_cast<Type*>(storage);

            return (*instance)(std::forward<Args>(args)...);
        }
    };

    template <typename Ret, typename... Args>
    Delegate<Ret(Args...)>::Delegate()
    {

        mID = currentTotalID++;

        new(&mData) std::nullptr_t(nullptr);

        mFunction = nullptr;
    }

    template <typename Ret, typename... Args>
    Delegate<Ret(Args...)>::Delegate(Delegate const& other)
    {
        if (other.mStored)
        {
            other.mCopyStorage(&other, this);

            mDestroyStorage = other.mDestroyStorage;
            mCopyStorage = other.mCopyStorage;
            mMoveStorage = other.mMoveStorage;
           
            mStored = true;
        }
        else
        {
            mData = other.mData;
         
        }

        mID = other.mID;
        mFunction = other.mFunction;
    }

    template <typename Ret, typename... Args>
    Delegate<Ret(Args...)>::Delegate(Delegate&& other)
    {
        if (other.mStored)
        {
            other.mMoveStorage(&other, this);

            mDestroyStorage = other.mDestroyStorage;
            mCopyStorage = other.mCopyStorage;
            mMoveStorage = other.mMoveStorage;
            mStored = true;
        }
        else
        {
            mData = other.mData;
        }

        mFunction = other.mFunction;
        mID = other.mID;
    }

    template <typename Ret, typename... Args>
    Delegate<Ret(Args...)>::~Delegate()
    {
        if (mStored)
            mDestroyStorage(this);
    }

    template <typename Ret, typename... Args>
    Delegate<Ret(Args...)>& Delegate<Ret(Args...)>::operator=(Delegate const& other)
    {
        Delegate temp(other);
        Swap(temp);

        return *this;
    }

    template <typename Ret, typename... Args>
    Delegate<Ret(Args...)>& Delegate<Ret(Args...)>::operator=(Delegate&& other)
    {
        Delegate temp(std::move(other));
        Swap(temp);

        return *this;
    }

    template <typename Ret, typename... Args>
    template <Ret(*FreeFunction)(Args...)>
    void Delegate<Ret(Args...)>::Bind()
    {
        new(&mData) std::nullptr_t(nullptr);

        mFunction = &Stub<FreeFunction>;
    }

    template <typename Ret, typename... Args>
    template <typename Type, Ret(Type::* PtrToMemFun)(Args...)>
    void Delegate<Ret(Args...)>::Bind(Type& instance)
    {
        new(&mData) Type* (&instance);

        mFunction = &Stub<Type, PtrToMemFun>;
    }

    template <typename Ret, typename... Args>
    template <typename Type, Ret(Type::* PtrToConstMemFun)(Args...) const>
    void Delegate<Ret(Args...)>::Bind(Type& instance)
    {
        new(&mData) Type* (&instance);

        mFunction = &Stub<Type, PtrToConstMemFun>;
    }

    template <typename Ret, typename... Args>
    template <typename Type>
    void Delegate<Ret(Args...)>::Bind(Type& funObj)
    {
        new(&mData) Type* (&funObj);

        mFunction = &Stub<Type>;
    }

    template <typename Ret, typename... Args>
    template <typename Type>
    void Delegate<Ret(Args...)>::Bind(Type&& funObj)
    {
        static_assert(sizeof(Type) <= sizeof(void*));

        new(&mData) Type(std::move(funObj));

        mFunction = &Stub<Type, Type>;

        mDestroyStorage = &DestroyStorage<Type>;
        mCopyStorage = &CopyStorage<Type>;
        mMoveStorage = &MoveStorage<Type>;

        mStored = true;
    }

    template <typename Ret, typename... Args>
    void Delegate<Ret(Args...)>::Swap(Delegate& other)
    {
        if (other.mStored)
            other.mCopyStorage(&other, this);
        else
            mData = other.mData;

        Function tempFunction = mFunction;
        mFunction = other.mFunction;
        other.mFunction = tempFunction;

        bool tempStored = mStored;
        mStored = other.mStored;
        other.mStored = tempStored;

        DestroyStorageFunction tempDestroyFun = mDestroyStorage;
        mDestroyStorage = other.mDestroyStorage;
        other.mDestroyStorage = tempDestroyFun;

        CopyStorageFunction tempCopyFun = mCopyStorage;
        mCopyStorage = other.mCopyStorage;
        other.mCopyStorage = tempCopyFun;

        MoveStorageFunction tempMoveFun = mMoveStorage;
        mMoveStorage = other.mMoveStorage;
        other.mMoveStorage = tempMoveFun;
    }


    template <typename Signature>
    class Signal;

    /**** signal partial class template for function types ****/
    template <typename Ret, typename... Args>
    class Signal<Ret(Args...)>
    {
    public:



        template <Ret(*FreeFunction)(Args...)>
        auto& Connect();

        template <typename Type, Ret(Type::* PtrToMemFun)(Args...)>
        auto& Connect(Type& instance);
   


        // template specialization which do nothing
        template <typename Type>
        void Connect(Type& instance)
        {

        }

        // variadic template pointers to non const member functions
        template <typename Type, Ret(Type::* PtrToMemFun)(Args...), Ret(Type::* PtrToMemFun_)(Args...), Ret(Type::* ... PtrToMemFun__)(Args...)>
        void Connect(Type& instance)
        {

            Connect<Type, PtrToMemFun>(instance);
            Connect<Type, PtrToMemFun_>(instance);
            Connect<Type, PtrToMemFun__...>(instance);
        }



        // variadic template pointers to const member functions
        template <typename Type, Ret(Type::* PtrToConstMemFun)(Args...) const, Ret(Type::* PtrToConstMemFun_)(Args...) const, Ret(Type::* ... PtrToConstMemFun__)(Args...) const>
        void Connect(Type& instance)
        {
            Connect<Type, PtrToConstMemFun>(instance);
            Connect<Type, PtrToConstMemFun_>(instance);
            Connect<Type, PtrToConstMemFun__...>(instance);
        }


        template <typename Type, Ret(Type::* PtrToConstMemFun)(Args...) const>
        auto& Connect(Type& instance);

        template <typename Type>
        auto& Connect(Type&& funObj);

        template <typename Type, Ret(Type::* PtrToMemFun)(Args...)>
        void Connect(const std::vector<Type>& types)
        {
            for (auto& p : types)
            {
                Connect<Type, PtrToMemFun>(const_cast<Type&>(p));
            }
        }

        template <typename Type, Ret(Type::* PtrToMemFun)(Args...) const>
        void Connect(const std::vector<Type>& types)
        {
            for (auto& p : types)
            {
                Connect<Type, PtrToMemFun>(const_cast<Type&>(p));
            }
        }
        
   

        void Disconnect(const Delegate<Ret(Args...)>& delegate)
        {
            mDelegates.erase(std::remove_if(mDelegates.begin(), mDelegates.end(), [&](const Delegate<Ret(Args...)>& d)

                {
                    return delegate.GetID() == d.GetID();

                }), mDelegates.end());
        }


        explicit operator bool() const { return !mDelegates.empty(); }

        void operator()(Args... args) { for (auto& delegate : mDelegates) delegate(std::forward<Args>(args)...); }

        void Fire(Args... args) { for (auto& delegate : mDelegates) delegate.Invoke(std::forward<Args>(args)...); }

        void Emit(Args... args) { for (auto& delegate : mDelegates) delegate.Invoke(std::forward<Args>(args)...); }

    private:
        std::vector<Delegate<Ret(Args...)>> mDelegates;
    };

    template <typename Ret, typename... Args>
    template <Ret(*FreeFunction)(Args...)>
    auto& Signal<Ret(Args...)>::Connect()
    {
        Delegate<Ret(Args...)> delegate;
        delegate.template Bind<FreeFunction>();
        mDelegates.push_back(delegate);
        return mDelegates.back();
        
    }

    template <typename Ret, typename... Args>
    template <typename Type, Ret(Type::* PtrToMemFun)(Args...)>
    auto& Signal<Ret(Args...)>::Connect(Type& instance)
    {
        Delegate<Ret(Args...)> delegate;
        delegate.template Bind<Type, PtrToMemFun>(instance);
        mDelegates.push_back(delegate);
        return mDelegates.back();
    }

    template <typename Ret, typename... Args>
    template <typename Type, Ret(Type::* PtrToConstMemFun)(Args...) const>
    auto& Signal<Ret(Args...)>::Connect(Type& instance)
    {
        Delegate<Ret(Args...)> delegate;
        delegate.template Bind<Type, PtrToConstMemFun>(instance);
        mDelegates.push_back(delegate);
        return mDelegates.back();
    }

    template <typename Ret, typename... Args>
    template <typename Type>
    auto& Signal<Ret(Args...)>::Connect(Type&& funObj)
    {
        Delegate<Ret(Args...)> delegate;
        delegate.template Bind<Type>(std::forward<Type>(funObj));
        mDelegates.push_back(delegate);
        return mDelegates.back();
    }


   // #define Connect(sender, signal, receiver, className ,slot) (sender).signal.Connect<className, slot>(receiver)

    //#define Connection(sender, signal, receiver, className,...) (sender).signal.Connect<className, __VA_ARGS__>(receiver)
    //#define Disconnection(sender, signal, delegate_) (sender).signal.Disconnect(delegate_)

    #define Connection(sender, signal, receiver, ...) (sender).signal.Connect<std::decay_t<decltype(receiver)>, __VA_ARGS__>(receiver)
    #define Disconnection(sender, signal, delegate_) (sender).signal.Disconnect(delegate_)


# if 0
	/*template<typename>class SlotBase;

	template<typename R, typename ... Args>
	class SlotBase<R(Args...)>
	{
	public:
		virtual R CallBackSlot(Args&& ... args) = 0;
		virtual ~SlotBase() = default;

	};


	template<typename Receiver, typename R, typename ... Args>
	class Slot : public SlotBase<R(Args...)>
	{

	public:
		Slot(Receiver* receiver, R(Receiver::* Func)(Args...))
		{
			this->m_Receiver = receiver;
			this->m_Func = Func;
		}

		R CallBackSlot(Args&& ... args)override
		{
			(m_Receiver->*m_Func)(std::forward<Args>(args)...);
		}

	private:
		Receiver* m_Receiver{};
		R(Receiver::* m_Func)(Args...);
	};


	template<typename> class Signal;
	template<typename R, typename ... Args>
	class Signal<R(Args...)>
	{

	public:
		template<typename Receiver>
		void RegisterSlot(Receiver* receiver, R(Receiver::* func)(Args...))
		{
			m_CallBackList.push_back(new Slot<Receiver, R, Args...>(receiver, func));
		}

		R operator()(Args&& ... args)
		{
			for (auto p : m_CallBackList)
			{
				p->CallBackSlot(std::forward<Args>(args)...);
			}
		}

		R Fire(Args&& ... args)
		{
			this->operator()(std::forward<Args>(args)...);
		}

	private:
		std::vector<SlotBase<R(Args...)>*> m_CallBackList;

	};*/


#endif

}