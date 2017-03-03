#include "http_listener.hpp"
#include "http_request.hpp"
#include "http_response.hpp"

#include <string>

Luvitronics::HttpListener::HttpListener(uint16_t port)
    : Task(), _server(port), _processorMap()
{
    _server.begin();
}

void Luvitronics::HttpListener::registerProcessor(String endpoint, RequestProcessor* processor)
{
    auto processor_ptr = std::unique_ptr<RequestProcessor>(processor);
    registerProcessor(endpoint, processor_ptr);
}

void Luvitronics::HttpListener::registerProcessor(String endpoint,
                                                  std::unique_ptr<RequestProcessor>& processor)
{
    _processorMap.emplace(endpoint, std::move(processor));
}

namespace {
    void dump_request(const Luvitronics::HttpRequest& request, Luvitronics::HttpResponse& response)
    {
        response = 200;
        response << "type: " << (int)request.type() << '\n';
        response << "path: " << request.path() << '\n';
        response << "object: " << request.object() << '\n';
        response << "version: " << request.version() << '\n' << '\n';
        
        for (const auto& pair : request.options()) {
            response << pair.first << " -> " << pair.second << '\n';
        }
        
        response << '\n' << request.body();
        return;
    }
}

void Luvitronics::HttpListener::process() {
    auto client = _server.available();
    
    if (!client)
        return;

    while (!client.available())
        delay(1);
    
    auto request = HttpRequest::create(client);
    auto response = HttpResponse(client);
    
    auto processor = _processorMap.find(request->path());
    if (processor != _processorMap.end()) 
        processor->second->process(*request, response);
    else
        response = 404;
    
    return;
    
    /*
    int pwm = atoi(line.c_str());
    if (0 < pwm && pwm < PWMRANGE) {
        client.print("read: ");
        client.println(pwm);
    }
    analogWrite(2, PWMRANGE - pwm);
    */
}
