### 一、客户端测试
------
1、（完成）成功调用客户端设置的回调函数

2、（完成）客户端请求数据分多次发送（使用tcp_send分多次发送数据）

3、（完成）客户端响应数据分多次发送，跨机器大数据包发送（构造超大响应包体）




### 二、连接池测试
------
1、（完成）获取、释放连接

2、（完成）连接池使用完毕复用测试

3、（完成）连接池扩容




### 三、协议
------
1、（完成）成功处理TCP协议

2、（完成）成功处理HTTP协议




### 四、缓冲区
------
1、（完成）可以处理较大缓冲区数据




### 五、协程
-----
1、（完成）connect第三方服务不存在

> 直接返回报错

2、（完成）connect第三方服务正常

> 正常请求第三方，正常解析第三方响应数据

3、（完成）客户端正常连接，第三方阻塞socket（定时器）超时

> 业务处理第三方连接代码处 返回错误，不影响客户端处理

4、（完成）客户端正常连接，第三方正常连接阻塞等待结果中，客户端socket（定时器）超时

> 客户端定时器超时，理论上应该立马结束请求。
>
> 但是coserver可能hook了第三方阻塞socket，hook处代码有可能有资源需要释放；
>
> 所以客户端定时器超时时，先恢复第三方阻塞代码，让业务代码继续执行，标识客户端连接dying，后续所有网络操作都立即返回错误，尽快结束当前请求。

5、（完成）业务主动yield，异步resume

> 主线程正常yield，异步线程进行异步resume通知

6、（完成）业务主动yield期间，客户端超时

> 断开和客户端之间的连接，等到客户端异步恢复时，检查连接版本号 不相等会直接忽略


### 六、子请求
-------
1、（完成）子请求正常处理

2、子请求写超时

3、（完成）子请求读超时

4、（完成）子请求正常连接，父请求超时

5、（完成）父请求超时时，有多个子请求

6、（未验证）父请求超时时，子请求还有子请求（目前不支持多层子请求）

7、（完成）子请求出错重试

8、（完成）子请求重试，获取连接失败

> 直接结束客户端请求

9、（完成）验证配置项功能正确

9.1、server

>（完成）max connections

9.2、子请求
>（完成）load balance

>（完成）fail timeout

>（完成）fail max num

>（完成）max connections

>（完成）connection max request

>（完成）connection max time

>（完成）retry times



### 七、多server
-------
1、（完成）detach请求

2、（完成）多个不同端口server handler处理函数


### 八、压力测试
-------
1、（完成）valgrind内存泄漏测试（重试）

2、（完成）大数据量发送测试



### Docker
------
docker build -t registry.cn-beijing.aliyuncs.com/mirror-docker-dong-ali/coserver:0.1 ./

docker run --name dev-coserver -itd --privileged=true -v ~/Github/CoServer:/home/liudong registry.cn-beijing.aliyuncs.com/mirror-docker-dong-ali/coserver:0.1 /bin/bash

docker exec -it dev-coserver /bin/bash

