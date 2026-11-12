#include "grpc_client.h"
#include <iostream>

namespace codesight
{

    GrpcClient::GrpcClient(const std::string &server_address)
    {
        auto channel = grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());
        stub_ = codesight::CodeSightService::NewStub(channel);
    }

    bool GrpcClient::sendParsedFile(const CppParsedFile &parsed)
    {
        codesight::ParsedFile request;
        request.set_filepath(parsed.filepath);

        for (const auto &f : parsed.functions)
        {
            auto *out = request.add_functions();
            out->set_name(f.name);
            out->set_line_number(f.line_number);
        }

        for (const auto &i : parsed.imports)
        {
            auto *out = request.add_imports();
            out->set_module(i.module);
            out->set_alias(i.alias);
            out->set_line_number(i.line_number);
        }

        for (const auto &r : parsed.api_routes)
        {
            auto *out = request.add_api_routes();
            out->set_method(r.method);
            out->set_path(r.path);
            out->set_handler(r.handler);
            out->set_line_number(r.line_number);
        }

        for (const auto &s : parsed.symbols)
        {
            auto *out = request.add_symbols();
            out->set_name(s.name);
            out->set_kind(s.kind);
            out->set_line_number(s.line_number);
            out->set_end_line(s.end_line);
            out->set_parent(s.parent);
            out->set_exported(s.exported);
            out->set_complexity(s.complexity);
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
