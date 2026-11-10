#pragma once

#include <string>
#include <memory>
#include <grpcpp/grpcpp.h>
#include "codesight.grpc.pb.h"

namespace codesight
{

    class GrpcClient
    {
    public:
        GrpcClient(const std::string &server_address);

        bool sendParsedFile(const std::string &filepath,
                            const std::vector<std::pair<std::string, int>> &functions,
                            const std::vector<std::string> &imports);

    private:
        std::unique_ptr<codesight::CodeSightService::Stub> stub_;
    };

} // namespace codesight
