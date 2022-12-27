#ifndef _CO_CALLBACK_EVENT_H_
#define _CO_CALLBACK_EVENT_H_

#include <cstdint>


namespace coserver
{

class CoConnection;

class CoCallbackEvent 
{
public:
    // 事件异常检查处理
    static int32_t event_exception(CoConnection* connection);
};

}

#endif //_CO_CALLBACK_EVENT_H_

