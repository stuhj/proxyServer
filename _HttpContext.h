#include<muduo/base/copyable.h>
#include<muduo/net/Buffer.h>
#include<utility>
#include<muduo/base/Timestamp.h>
using namespace muduo;
using namespace muduo::net;

class _HttpContext:public muduo::copyable
{
public:
    enum HttpRequestParseState
    {
        kExpectRequestLine,
        kExpectHeaders,
        kExpectBody,
        kGotAll,
    };

    _HttpContext()
        :state_(kExpectRequestLine)
    {        
    }

    std::pair<bool,int> parseRequest(Buffer*buf);
    //bool parseRequest(Buffer*buf,Timestamp receiveTime);
    bool gotAll()   const
    {return state_ == kGotAll;}

    void reset()
    {
        state_ = kExpectRequestLine;
    }

    HttpRequestParseState getStates()
    {
        return state_;
    }

private:

    HttpRequestParseState state_;
    
    bool processRequestLine(const char* begin,const char* end);
    
};