#include "crow.h"
#include "router.h"

int main() {

    crow::SimpleApp app;

    CROW_ROUTE(app, "/chat").methods("POST"_method)
    ([](const crow::request& req){

        auto body = crow::json::load(req.body);

        std::string prompt = body["prompt"].s();

        std::string response = route_request(prompt);

        crow::json::wvalue result;
        result["response"] = response;

        return result;
    });

    app.port(8000).multithreaded().run();
}