#include<muduo/net/TcpServer.h>

using namespace muduo;
using namespace muduo::net;
class _HttpRequest;

void getMemoryUsedNow();
unsigned int getMemoryToInt(std::string _memoryUsed);
void setEnvironment();

class ProxyServer:public muduo::noncopyable
{
private:

    TcpServer server_;
   // HttpCallback httpCallback_;
    //作为Server的连接回调和消息回调
    void onServerConnection(const TcpConnectionPtr& conn);
    void onServerMessage(const TcpConnectionPtr& conn, Buffer*buf,Timestamp);
   // void onServerHighWaterMark(const muduo::net::TcpConnectionPtr& conn);
   // void onServerWriteComplete(const muduo::net::TcpConnectionPtr& conn);

public:
    ProxyServer(EventLoop*loop,
                const InetAddress& listenAddr,
                const string& name,
                TcpServer::Option option = TcpServer::kNoReusePort);

    EventLoop* getLoop() const {return server_.getLoop();}

/*    void setHttpCallback(const HttpCallback& cb)
    {
        httpCallback_ = cb;
    }
*/
    void setThreadNum(int numThread)
    {
        server_.setThreadNum(numThread);
    }

    void start()
    {
        server_.start();
    }

};

