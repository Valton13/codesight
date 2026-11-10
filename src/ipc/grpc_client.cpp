#include "grpc_client.h"
#include <iostream>

namespace codesight
{

    GrpcClient::GrpcClient(const std::string &server_address)
    {
        auto channel = grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());
        stub_ = codesight::CodeSightService::NewStub(channel);
    }

    bool GrpcClient::sendParsedFile(const std::string &filepath,
                                    const std::vector<std::pair<std::string, int>> &functions,
                                    const std::vector<std::string> &imports)
    {
        codesight::ParsedFile request;
        request.set_filepath(filepath);

        // Add functions
        for (const auto &[name, line] : functions)
        {
            auto *func = request.add_functions();
            func->set_name(name);
            func->set_line_number(line);
        }

        // Add imports
        for (const auto &imp : imports)
        {
            auto *import_info = request.add_imports();
            import_info->set_module(imp);
        }

        codesight::AckResponse response;
        grpc::ClientContext context;

        grpc::Status status = stub_->SendParsedFile(&context, request, &response);

        if (!status.ok())
        {
            std::cerr << "gRPC error: " << status.error_message() << "\n";
            return false;
        }

        return response.success();
    }

} // namespace codesight
