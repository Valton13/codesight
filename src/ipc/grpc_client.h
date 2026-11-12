#pragma once

#include <string>
#include <memory>
#include <grpcpp/grpcpp.h>
#include "codesight.grpc.pb.h"
#include "../parser/parser.h"

namespace codesight
{

    class GrpcClient
    {
    public:
        explicit GrpcClient(const std::string &server_address);

        // Sends the full parse record: functions, imports, and symbols.
        bool sendParsedFile(const CppParsedFile &parsed);

    private:
        std::unique_ptr<codesight::CodeSightService::Stub> stub_;
    };

} // namespace codesight
