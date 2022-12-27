#ifndef _CO_COROUTINE_H_
#define _CO_COROUTINE_H_

#include <ucontext.h>
#include <functional>


namespace coserver
{

enum CoroutineStatus 
{
    COROUTINE_READY = 1,
    COROUTINE_RUNNING,
    COROUTINE_SUSPEND,
    COROUTINE_DOWN
};


struct CoCoroutine
{
    CoroutineStatus     m_coroutineStatus = CoroutineStatus::COROUTINE_READY;

    ucontext_t          m_context;
    std::function<void()>  m_func   = NULL;

    // 协程内部堆栈  用于切出时临时保存协程信息
    char*           m_interStack    = NULL;
    uint32_t        m_interStackCap = 0;
    uint32_t        m_interStackSize= 0;


    CoCoroutine();
    ~CoCoroutine();

    void reset();
};


class CoCoroutineMain
{
public:
    CoCoroutineMain();
    ~CoCoroutineMain();

    int32_t init(uint32_t sharedStackSize);

    // 初始化协程
    int32_t init_coroutine(std::function<void()> const &func, CoCoroutine* coroutine); // const important

    // 切入 resume
    int32_t swap_in(CoCoroutine* coroutine);

    // 切出 yield
    int32_t swap_out(CoCoroutine* coroutine);


private:
    ucontext_t      m_contextMain;

    char*           m_sharedStack   = NULL; // 共享栈空间
    uint32_t        m_sharedStackSize = 0;  // 共享栈大小
};

}

#endif //_CO_COROUTINE_H_

