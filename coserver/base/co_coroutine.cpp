#include <string.h>
#include <assert.h>
#include "base/co_coroutine.h"
#include "base/co_common.h"


namespace coserver
{

const int32_t MIN_SHARED_STACK_SIZE = 1024;            // 1k
const int32_t MAX_SHARED_STACK_SIZE = 10*1024*1024;    // 10M
const int32_t BEST_SHARED_STACK_SIZE = 1024*1024;      // 1M


CoCoroutine::CoCoroutine()
{
}

CoCoroutine::~CoCoroutine()
{
    SAFE_FREE(m_interStack);
}

void CoCoroutine::reset()
{
    m_coroutineStatus = CoroutineStatus::COROUTINE_READY;
    m_func = NULL;
}


CoCoroutineMain::CoCoroutineMain()
{
}

CoCoroutineMain::~CoCoroutineMain()
{
    SAFE_DELETE_ARRAY(m_sharedStack);
}

int32_t CoCoroutineMain::init(uint32_t sharedStackSize)
{
    // shared stack
    m_sharedStackSize = sharedStackSize < MIN_SHARED_STACK_SIZE ? BEST_SHARED_STACK_SIZE : sharedStackSize;
    m_sharedStackSize = sharedStackSize > MAX_SHARED_STACK_SIZE ? BEST_SHARED_STACK_SIZE : sharedStackSize;
    m_sharedStack = new char[m_sharedStackSize];

    return CO_OK;
}

static void func_context(std::function<void()> *func)
{
    (*func)();
}

int32_t CoCoroutineMain::init_coroutine(std::function<void()> const &func, CoCoroutine* coroutine)
{
    if (-1 == getcontext(&(coroutine->m_context)))
        return CO_ERROR;

    // param
    coroutine->m_func = func;

    // init m_context
    coroutine->m_context.uc_stack.ss_sp = m_sharedStack;
    coroutine->m_context.uc_stack.ss_size = m_sharedStackSize;
    coroutine->m_context.uc_link = NULL;
    makecontext(&(coroutine->m_context), (void(*)(void)) &func_context, 1, &(coroutine->m_func));
    return CO_OK;
}

int32_t CoCoroutineMain::swap_in(CoCoroutine* coroutine)
{
    // 将备份栈数据拷贝回协程运行栈上  继续执行
    if (coroutine->m_interStackSize) {
        memcpy(m_sharedStack + m_sharedStackSize - coroutine->m_interStackSize, coroutine->m_interStack, coroutine->m_interStackSize);
    }
    return swapcontext(&m_contextMain, &(coroutine->m_context));
}

int32_t CoCoroutineMain::swap_out(CoCoroutine* coroutine)
{
    // 共享栈保存 使用的堆栈数据
    char dummy = 0;
    char* top = m_sharedStack + m_sharedStackSize;
    uint32_t currentStackSize = top - &dummy;
    assert(currentStackSize <= m_sharedStackSize);    // 栈溢出 异常
    
    if (coroutine->m_interStackCap < currentStackSize) {
        // optimize
        coroutine->m_interStack = (char*)realloc(coroutine->m_interStack, currentStackSize);
        coroutine->m_interStackCap = currentStackSize;
    }
    coroutine->m_interStackSize = currentStackSize;
    memcpy(coroutine->m_interStack, &dummy, coroutine->m_interStackSize);

    return swapcontext(&(coroutine->m_context), &m_contextMain);
}

}

