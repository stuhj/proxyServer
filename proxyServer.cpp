#include"proxyServer.h"
#include"tunnel.h"
#include<muduo/net/Buffer.h>
#include<muduo/base/Logging.h>
#include<fstream>

using namespace std::placeholders;
using namespace muduo;
using namespace muduo::net;
EventLoop* g_eventLoop;
InetAddress* g_serverAddr;
std::map<string,TunnelPtr> g_tunnels;
unsigned int g_memoryUsed;
pid_t g_pid;
std::string g_memoryFileName; 
const int VmRSSLine = 21;
//更新当前内存使用情况: g_memoryUsed

unsigned int getMemoryToInt(std::string _memoryUsed)
{
    std::string::iterator first = 
        std::find(_memoryUsed.begin(),_memoryUsed.end(),':');
    std::string key(_memoryUsed.begin(),first);
    if(key == "VmRSS")
    {
        std::string::reverse_iterator last1 =
            std::find(_memoryUsed.rbegin(),_memoryUsed.rend(),' ');
        std::string::reverse_iterator last2 = 
            std::find(last1+1,_memoryUsed.rend(),' ');
        g_memoryUsed = std::atoi(std::string(&*last2,&*last1).c_str());
    }
    else
    {
        abort();
    }
    return g_memoryUsed;
    
}
void getMemoryUsedNow()
{
    std::fstream memoryFile;
   // LOG_INFO<<"STATE:   GETMEMORYFILE";
    memoryFile.open(g_memoryFileName,std::ios::in);    
    //文件打开失败
    if(!memoryFile)
    {
        //LOG_INFO<<"OPEN FILE FAIL";
        abort();
    }
    //LOG_INFO<<"OPEN FILE SUCCESSFUL";
    //查找VmRSS
    int line = 0;
    std::string _memoryUsed;
    while(std::getline(memoryFile,_memoryUsed))
    {
        line++;
        if(line == VmRSSLine)
        {
            //会有race condition 需要加锁
            LOG_INFO<<_memoryUsed;
            g_memoryUsed = getMemoryToInt(_memoryUsed);
            //LOG_INFO<<"g_memoryUsed: "<<g_memoryUsed;
            break;
        }
    }    
}

ProxyServer::ProxyServer(EventLoop*loop,
                const InetAddress& listenAddr,
                const string& name,
                TcpServer::Option option)
:server_(loop,listenAddr,name,option)
{
    server_.setConnectionCallback(
        std::bind(&ProxyServer::onServerConnection,this,_1));
    server_.setMessageCallback(
        std::bind(&ProxyServer::onServerMessage,this,_1,_2,_3));

    //开启内存监控
    setEnvironment();
    loop->runEvery(8,getMemoryUsedNow);
}


void ProxyServer::onServerConnection(const TcpConnectionPtr& conn)
{
    LOG_INFO<<"SERVER CONNECTION CALLBACK";
    if(conn->connected())
    {
        if(g_memoryUsed < 1024*256)
        {
            LOG_INFO<<"Set No Delay";
            conn->setTcpNoDelay(true);
            conn->startRead();
        }
        else
        {
            LOG_INFO<<"MEMORY OVERLOAD :SHUTDOWN";
            conn->shutdown();
        }
    }
    else
    {
        //LOG_INFO<<"INTO DISCONNECTION STATE1";
        auto i = g_tunnels.find(conn->name());
        LOG_INFO<<"HAS TUNNEL: "<<(i==g_tunnels.end()?"false":"ture");
        if(i != g_tunnels.end())
        {
            g_tunnels[conn->name()]->disconnect();
            g_tunnels.erase(conn->name());
        }
    }
}

void ProxyServer::onServerMessage(const TcpConnectionPtr& conn,Buffer*buf,Timestamp receiveTime)
{
    //LOG_INFO<<"Http Message Recieve";
    std::shared_ptr<_HttpContext>context(new _HttpContext());
    std::pair<bool,int> info;
    while(conn->connected() && 
            (info = context->parseRequest(buf)).first)  //解析请求是完整的
    {
        LOG_INFO<<"Http Request Complete";
        if(conn->getContext().empty())
        {
            //建立到后端的连接，并发送
            //shared_ptr
            TunnelPtr tunnel(new Tunnel(g_eventLoop,*g_serverAddr,conn));
            tunnel->setup();
            tunnel->connect();
            //将这个tunnel加入到映射表中
            //tunnel生命期被延长
            LOG_INFO<<"SET G_TUNNELS";
            g_tunnels[conn->name()] = tunnel;
            break;
        }
        else
        {
            //转发数据到后端
            //获取到后端的连接
            const TcpConnectionPtr& clientConn = 
                boost::any_cast<const TcpConnectionPtr&>(conn->getContext());
            LOG_INFO<<"GET to server connection";
            clientConn->send(buf,info.second);
            //if(context->gotAll())
            {
                context->reset();
            }
        }
    }
}
/*
void ProxyServer::onServerHighWaterMark(const muduo::net::TcpConnectionPtr& conn)
{
    std::shared_ptr<_HttpContext>context(new _HttpContext());
    context->parseRequest(buf));
    //如果已经触发回调，却不再body处，就属于无效数据，断开连接
    if(context->states_ != _HttpContext::kExpectBody)
    {
        conn->disconnect();
    }
}
*/
void setEnvironment()
{
    g_pid = getpid();
    g_memoryFileName += ("/proc/"+std::string(std::to_string(g_pid))+"/status");
    LOG_INFO<<"memoryFileName: "<<g_memoryFileName; 
}
int main()
{
    EventLoop loop;
    g_eventLoop = &loop;


    InetAddress serverAddr("127.0.0.1",8000);
    g_serverAddr = &serverAddr;

    ProxyServer server(&loop,InetAddress(8888),"ProxyServer");
    server.setThreadNum(2);
    server.start();
    loop.loop();
    return 0;
}

