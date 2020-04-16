#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <improbable/worker.h>
#include <improbable/standard_library.h>
#include <improbable/view.h>
#include <iostream>
#include <thread>
#include <deer.h>
#include <hunter.h>

// Use this to make a worker::ComponentRegistry.
// For example use worker::Components<improbable::Position, improbable::Metadata> to track these common components
using ComponentRegistry = worker::Components<
    deer::Health, 
    deer::Dialogue,
    hunter::Health, 
    hunter::Name, 
    improbable::Position, 
    improbable::EntityAcl, 
    improbable::Metadata, 
    improbable::Interest
>;

// Constants and parameters
const int ErrorExitStatus = 1;
const std::string kLoggerName = "startup.cc";
const std::uint32_t kGetOpListTimeoutInMilliseconds = 100;

worker::Connection ConnectWithReceptionist(const std::string hostname,
                                           const std::uint16_t port,
                                           const std::string& worker_id,
                                           const worker::ConnectionParameters& connection_parameters) {
    auto future = worker::Connection::ConnectAsync(ComponentRegistry{}, hostname, port, worker_id, connection_parameters);
    return future.Get();
}

std::string get_random_characters(size_t count) {
    const auto randchar = []() -> char {
        const char charset[] =
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";
        const auto max_index = sizeof(charset) - 1;
        return charset[std::rand() % max_index];
    };
    std::string str(count, 0);
    std::generate_n(str.begin(), count, randchar);
    return str;
}

void SendDeerCommandRequest(worker::Connection& connection, worker::View& view) {
    const worker::Option<uint32_t> timeout_ms {500};

    for (auto it = view.Entities.begin(); it != view.Entities.end(); it++) {
        auto entity_id = it -> first;
        auto request = connection.SendCommandRequest<deer::Health::Commands::GotShot>(
            entity_id, deer::Health::Commands::GotShot::Request { deer::Shot{15} },
            timeout_ms,
            {}
        );

        std::cout << "Command sent: " << request.GetValue().Id << std::endl;
    }
}

// Entry point
int main(int argc, char** argv) {
    auto now = std::chrono::high_resolution_clock::now();
    std::srand(std::chrono::time_point_cast<std::chrono::nanoseconds>(now).time_since_epoch().count());

    std::cout << "[local] Worker started " << std::endl;

    auto print_usage = [&]() {
        std::cout << "Usage: Managed receptionist <hostname> <port> <worker_id>" << std::endl;
        std::cout << std::endl;
        std::cout << "Connects to SpatialOS" << std::endl;
        std::cout << "    <hostname>      - hostname of the receptionist or locator to connect to.";
        std::cout << std::endl;
        std::cout << "    <port>          - port to use if connecting through the receptionist.";
        std::cout << std::endl;
        std::cout << "    <worker_id>     - (optional) name of the worker assigned by SpatialOS." << std::endl;
        std::cout << std::endl;
    };

    std::vector<std::string> arguments;

    // if no arguments are supplied, use the defaults for a local deployment
    if (argc == 1) {
        arguments = { "receptionist", "localhost", "7777" };
    } else {
        arguments = std::vector<std::string>(argv + 1, argv + argc);
    }

    if (arguments.size() != 4 && arguments.size() != 3) {
        print_usage();
        return ErrorExitStatus;
    }

    worker::ConnectionParameters parameters;
    parameters.WorkerType = "Managed";
    parameters.Network.ConnectionType = worker::NetworkConnectionType::kTcp;
    parameters.Network.UseExternalIp = false;

    std::string workerId;

    // When running as an external worker using 'spatial local worker launch'
    // The WorkerId isn't passed, so we generate a random one
    if (arguments.size() == 4) {
        workerId = arguments[3];
    } else {
        workerId = parameters.WorkerType + "_" + get_random_characters(4);
    }

    std::cout << "[local] Connecting to SpatialOS as " << workerId << "..." << std::endl;

    // Connect with receptionist
    worker::Connection connection = ConnectWithReceptionist(arguments[1], atoi(arguments[2].c_str()), workerId, parameters);

    connection.SendLogMessage(worker::LogLevel::kInfo, kLoggerName, "Connected successfully");

    // Register callbacks and run the worker main loop.
    worker::View view{ ComponentRegistry{} };
    bool is_connected = connection.IsConnected();

    view.OnDisconnect([&](const worker::DisconnectOp& op) {
        std::cerr << "[disconnect] " << op.Reason << std::endl;
        is_connected = false;
    });

    // Print log messages received from SpatialOS
    view.OnLogMessage([&](const worker::LogMessageOp& op) {
        if (op.Level == worker::LogLevel::kFatal) {
            std::cerr << "Fatal error: " << op.Message << std::endl;
            std::terminate();
        }
        std::cout << "[remote] " << op.Message << std::endl;
    });

    //Process any deer::SaidSomething events, part of the deer::Dialogue component
    view.OnComponentUpdate<deer::Dialogue>(
        [](const worker::ComponentUpdateOp<deer::Dialogue>& op) {
            std::cout << "Processing event ops..." << std::endl;
            //op.Update.said_something will contain a list of all SaidSomething events
            for (auto it : op.Update.said_something()) {
                std::cout << "Deer dialogue event: " << it.message() << std::endl;
            }
        }
    );

    view.OnComponentUpdate<deer::Health>(
        [](const worker::ComponentUpdateOp<deer::Health>& op) {
            std::cout << "Processing event ops..." << std::endl;
            for (auto it : op.Update.remaining_health()) {
                std::cout << "Deer health event: " << it << std::endl;
            }
        }
    );

    view.OnCommandResponse<deer::Health::Commands::GotShot>(
        [](const worker::CommandResponseOp<deer::Health::Commands::GotShot>& op) {
            std::cout << "Received response for command: " << op.RequestId.Id << std::endl;
        }
    );

    if (is_connected) {
        std::cout << "[local] Connected successfully to SpatialOS, listening to ops... " << std::endl;
    }

    std::cout << "[local] Starting game loop!" << std::endl;

    hunter::Name::Update hunter_name_update;

    //This is the game loop :)
    while (is_connected) {
        //dispatcher.Process(connection.GetOpList(kGetOpListTimeoutInMilliseconds));
        //The ops list is so the connection doesn't time out
        auto ops = connection.GetOpList(kGetOpListTimeoutInMilliseconds);

        //Process ops so entities and components get added automatically
        view.Process(ops);

        for (auto it = view.Entities.begin(); it != view.Entities.end(); it++) {
            auto entity_id = it -> first;

            hunter_name_update.set_first_name(get_random_characters(5));
            hunter_name_update.set_last_name(get_random_characters(8));

            connection.SendComponentUpdate<hunter::Name>(entity_id, hunter_name_update);
        }

        SendDeerCommandRequest(connection, view);

        //Now go to sleep for a bit to avoid excess changes
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    return ErrorExitStatus;
}

