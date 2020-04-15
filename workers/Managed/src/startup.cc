#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <improbable/worker.h>
#include <improbable/standard_library.h>
#include <improbable/view.h>
#include <iostream>
#include <thread>
#include <special.h>
#include <dummy.h>

// Use this to make a worker::ComponentRegistry.
// For example use worker::Components<improbable::Position, improbable::Metadata> to track these common components
using ComponentRegistry = worker::Components<dummy::Name, special::Health, special::Name, improbable::Position, improbable::EntityAcl, improbable::Metadata, improbable::Interest>;

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

class SpecialTest {
    public:
        uint32_t health;
        std::string firstName;
        std::string lastName;

    SpecialTest(uint32_t _health, std::string _firstName, std::string _lastName) {
        health = _health;
        firstName = _firstName;
        lastName = _lastName;
    }
};

void AddSpecialEntityAcl(worker::Entity& entity) {
    static const std::string simulation = "simulation";
    static const std::string client = "client";
    static const std::string dummy = "dummy";

    //Start with the attribute set based on the worker attribute (defined in spatialos worker JSON)
    worker::List<std::string> simulationWorkerAttributeSet{simulation};
    worker::List<std::string> clientWorkerAttributeSet{client};
    worker::List<std::string> dummyWorkerAttributeSet{dummy};

    //Requirement set to match any worker with 'simulation' attribute
    improbable::WorkerRequirementSet simulationWorkerRequirementSet{
        worker::List<improbable::WorkerAttributeSet>{{simulationWorkerAttributeSet}}
    };

    //Match clients
    improbable::WorkerRequirementSet clientWorkerRequirementSet{
        worker::List<improbable::WorkerAttributeSet>{{clientWorkerAttributeSet}}
    };

    improbable::WorkerRequirementSet dummyWorkerRequirementSet{
        worker::List<improbable::WorkerAttributeSet>{{dummyWorkerAttributeSet}}
    };

    //Match any 'client' or 'simulation' worker attributes
    improbable::WorkerRequirementSet clientOrSimulationRequirementSet{
        worker::List<improbable::WorkerAttributeSet>{
            clientWorkerAttributeSet,
            simulationWorkerAttributeSet
        },
    };

    improbable::WorkerRequirementSet allWorkersRequirementSet{
        worker::List<improbable::WorkerAttributeSet>{
            clientWorkerAttributeSet,
            simulationWorkerAttributeSet,
            dummyWorkerAttributeSet
        },
    };

    //Give authority over Position, EntityAcl, Health and Name fields:
    worker::Map<worker::ComponentId, improbable::WorkerRequirementSet> componentAcl {
        {improbable::Position::ComponentId, simulationWorkerRequirementSet},
        {improbable::EntityAcl::ComponentId, simulationWorkerRequirementSet},
        {special::Health::ComponentId, simulationWorkerRequirementSet},
        {special::Name::ComponentId, simulationWorkerRequirementSet}
    };

    //Add to the EntityACl component, Read access for the clientOrSimulationRequirementSet and write access for componentAcl
    entity.Add<improbable::EntityAcl>(improbable::EntityAcl::Data{
                                        /*Read access Here*/ allWorkersRequirementSet, 
                                        /*Write access Here*/ componentAcl});
}

void AddDummyEntityAcl(worker::Entity& entity) {

    static const std::string simulation = "simulation";
    static const std::string dummy = "dummy";

    //Start with the attribute set based on the worker attribute (defined in spatialos worker JSON)
    worker::List<std::string> simulationWorkerAttributeSet{simulation};
    worker::List<std::string> dummyWorkerAttributeSet{dummy};

    //Requirement set to match any worker with 'simulation' attribute
    improbable::WorkerRequirementSet simulationWorkerRequirementSet{
        worker::List<improbable::WorkerAttributeSet>{{simulationWorkerAttributeSet}}
    };

    improbable::WorkerRequirementSet dummyWorkerRequirementSet{
        worker::List<improbable::WorkerAttributeSet>{{dummyWorkerAttributeSet}}
    };

    worker::Map<worker::ComponentId, improbable::WorkerRequirementSet> componentAcl {
        {dummy::Name::ComponentId, dummyWorkerRequirementSet}
    };

    entity.Add<improbable::EntityAcl>(improbable::EntityAcl::Data{
                                        /*Read access Here*/ simulationWorkerRequirementSet, 
                                        /*Write access Here*/ componentAcl});
}

void CreateSpecialEntity(worker::Connection& connection, worker::View& view, SpecialTest specialTest) {
    std::cout << "Starting special entity creation... " << std::endl;
    //Entity Creation
    worker::RequestId<worker::CreateEntityRequest> entity_creation_request_id;

    //First, reserve 1 Entity ID (timeout 500 ms)
    worker::RequestId<worker::ReserveEntityIdsRequest> entity_id_reservation_request_id = connection.SendReserveEntityIdsRequest(1, 500);

    //Next, we create an entity with the reserved ID.
    //This registers a function as the callback when entity ID reservation is successful
    view.OnReserveEntityIdsResponse([entity_id_reservation_request_id, &connection, &entity_creation_request_id, specialTest](const worker::ReserveEntityIdsResponseOp& op){
        if (op.RequestId == entity_id_reservation_request_id && op.StatusCode == worker::StatusCode::kSuccess) {
            worker::Entity entity;
            entity.Add<improbable::Position>({{1, 2, 3}});
            entity.Add<special::Health>({specialTest.health});
            entity.Add<special::Name>({specialTest.firstName, specialTest.lastName});
            AddSpecialEntityAcl(entity);

            auto result = connection.SendCreateEntityRequest(entity, op.FirstEntityId, 500);
            if (result) {
                connection.SendLogMessage(worker::LogLevel::kDebug, "Creating Entity", "Successfully created entity");
                std::cout << "[local] Successful special entity creation!" << std::endl;
                entity_creation_request_id = *result;
            } else {
                connection.SendLogMessage(worker::LogLevel::kError, "Creating Entity", result.GetErrorMessage());
                std::cout << "[local] Failed to create entity: " << result.GetErrorMessage() << std::endl;
                std::terminate();
            }
        }
    });
}

void AddDummyEntityInterestSphere(worker::Entity& entity) {
    std::cout << "Adding entity sphere interest..." << std::endl;

    const worker::Option<improbable::ComponentInterest_SphereConstraint> sphere_constraint{
            improbable::ComponentInterest_SphereConstraint(improbable::Coordinates{1, 2, 3}, 1000)
    };

    const worker::Option<worker::EntityId> entity_id_constraint{
        500
    };

    const worker::Option<uint32_t> special_health_component_constraint{
        special::Health::ComponentId
    };

    improbable::ComponentInterest_QueryConstraint query_constraint(
        /*Sphere*/ sphere_constraint,
        /*Cylinder*/{},
        /*Box*/ {},
        /*Relative Sphere*/ {},
        /*Relative Cylinder*/ {},
        /*Relative Box*/ {},
        /*Entity ID*/ {},
        /*Component ID*/ {},
        /*And Constraints*/ {},
        /*Or Constraints*/ {}
    );

    auto dummy_interest = improbable::ComponentInterest {
        worker::List<improbable::ComponentInterest_Query> {
            improbable::ComponentInterest_Query {
                /*Constraint*/ query_constraint,
                /*Full Snapshot*/ worker::Option<bool> {false},
                /*Result Component IDs*/ worker::List<uint32_t>{
                    special::Health::ComponentId,
                    special::Name::ComponentId,
                    improbable::Position::ComponentId,
                    improbable::EntityAcl::ComponentId
                },
                /*Frequency*/ worker::Option<float> {30}
            }
        }
    };

    //Grant 'interest' to the dummy::Name component, so anything w/ write access authority
    //over dummy::Name gets interested
    entity.Add<improbable::Interest>(
        improbable::InterestData {
            worker::Map<uint, improbable::ComponentInterest> {
                {dummy::Name::ComponentId, dummy_interest}
            }
        }
    );

    std::cout << "Entity sphere interest added!" << std::endl;
}

void CreateDummyEntity(worker::Connection& connection, worker::View& view, std::string name) {
    std::cout << "Starting dummy entity creation... " << std::endl;
    //Entity Creation
    worker::RequestId<worker::CreateEntityRequest> entity_creation_request_id;

    //First, reserve 1 Entity ID (timeout 500 ms)
    worker::RequestId<worker::ReserveEntityIdsRequest> entity_id_reservation_request_id = connection.SendReserveEntityIdsRequest(1, 500);

    //Next, we create an entity with the reserved ID.
    //This registers a function as the callback when entity ID reservation is successful
    view.OnReserveEntityIdsResponse([entity_id_reservation_request_id, &connection, &entity_creation_request_id, name](const worker::ReserveEntityIdsResponseOp& op){
        if (op.RequestId == entity_id_reservation_request_id && op.StatusCode == worker::StatusCode::kSuccess) {
            worker::Entity entity;
            entity.Add<improbable::Position>({{4, 5, 6}});
            entity.Add<dummy::Name>({name});
            AddDummyEntityAcl(entity);
            AddDummyEntityInterestSphere(entity);

            auto result = connection.SendCreateEntityRequest(entity, op.FirstEntityId, 500);
            if (result) {
                connection.SendLogMessage(worker::LogLevel::kDebug, "Creating Entity", "Successfully created entity");
                std::cout << "[local] Successful dummy entity creation!" << std::endl;
                entity_creation_request_id = *result;
            } else {
                connection.SendLogMessage(worker::LogLevel::kError, "Creating Entity", result.GetErrorMessage());
                std::cout << "[local] Failed to create entity: " << result.GetErrorMessage() << std::endl;
                std::terminate();
            }
        }
    });
}

void AddExternalEntityAcl(worker::Entity& entity) {

    static const std::string simulation = "simulation";
    static const std::string client = "client";

    //Start with the attribute set based on the worker attribute (defined in spatialos worker JSON)
    worker::List<std::string> simulationWorkerAttributeSet{simulation};
    worker::List<std::string> clientWorkerAttributeSet{client};

    //Requirement set to match any worker with 'simulation' attribute
    improbable::WorkerRequirementSet simulationWorkerRequirementSet{
        worker::List<improbable::WorkerAttributeSet>{{clientWorkerAttributeSet}}
    };

    improbable::WorkerRequirementSet clientWorkerRequirementSet{
        worker::List<improbable::WorkerAttributeSet>{{clientWorkerAttributeSet}}
    };

    worker::Map<worker::ComponentId, improbable::WorkerRequirementSet> componentAcl {
        {dummy::Name::ComponentId, clientWorkerRequirementSet}
    };

    entity.Add<improbable::EntityAcl>(improbable::EntityAcl::Data{
                                        /*Read access Here*/ simulationWorkerRequirementSet, 
                                        /*Write access Here*/ componentAcl});
}

void AddExternalEntityInterestSphere(worker::Entity& entity) {
    std::cout << "Adding entity sphere interest..." << std::endl;

    const worker::Option<improbable::ComponentInterest_SphereConstraint> sphere_constraint{
            improbable::ComponentInterest_SphereConstraint(improbable::Coordinates{1, 2, 3}, 1000)
    };

    const worker::Option<worker::EntityId> entity_id_constraint{
        500
    };

    const worker::Option<uint32_t> special_health_component_constraint{
        special::Health::ComponentId
    };

    improbable::ComponentInterest_QueryConstraint query_constraint(
        /*Sphere*/ sphere_constraint,
        /*Cylinder*/{},
        /*Box*/ {},
        /*Relative Sphere*/ {},
        /*Relative Cylinder*/ {},
        /*Relative Box*/ {},
        /*Entity ID*/ {},
        /*Component ID*/ {},
        /*And Constraints*/ {},
        /*Or Constraints*/ {}
    );

    auto dummy_interest = improbable::ComponentInterest {
        worker::List<improbable::ComponentInterest_Query> {
            improbable::ComponentInterest_Query {
                /*Constraint*/ query_constraint,
                /*Full Snapshot*/ worker::Option<bool> {false},
                /*Result Component IDs*/ worker::List<uint32_t>{
                    special::Health::ComponentId,
                    special::Name::ComponentId,
                    improbable::Position::ComponentId,
                    improbable::EntityAcl::ComponentId
                },
                /*Frequency*/ worker::Option<float> {30}
            }
        }
    };

    //Grant 'interest' to the dummy::Name component, so anything w/ write access authority
    //over dummy::Name gets interested
    entity.Add<improbable::Interest>(
        improbable::InterestData {
            worker::Map<uint, improbable::ComponentInterest> {
                {dummy::Name::ComponentId, dummy_interest}
            }
        }
    );

    std::cout << "Entity sphere interest added!" << std::endl;
}

void CreateExternalEntity(worker::Connection& connection, worker::View& view, std::string name) {
    std::cout << "Starting external entity creation... " << std::endl;
    //Entity Creation
    worker::RequestId<worker::CreateEntityRequest> entity_creation_request_id;

    //First, reserve 1 Entity ID (timeout 500 ms)
    worker::RequestId<worker::ReserveEntityIdsRequest> entity_id_reservation_request_id = connection.SendReserveEntityIdsRequest(1, 500);

    //Next, we create an entity with the reserved ID.
    //This registers a function as the callback when entity ID reservation is successful
    view.OnReserveEntityIdsResponse([entity_id_reservation_request_id, &connection, &entity_creation_request_id, name](const worker::ReserveEntityIdsResponseOp& op){
        if (op.RequestId == entity_id_reservation_request_id && op.StatusCode == worker::StatusCode::kSuccess) {
            worker::Entity entity;
            entity.Add<improbable::Position>({{4, 5, 6}});
            entity.Add<dummy::Name>({name});
            AddExternalEntityAcl(entity);
            AddExternalEntityInterestSphere(entity);

            auto result = connection.SendCreateEntityRequest(entity, op.FirstEntityId, 500);
            if (result) {
                connection.SendLogMessage(worker::LogLevel::kDebug, "Creating Entity", "Successfully created entity");
                std::cout << "[local] Successful external entity creation!" << std::endl;
                entity_creation_request_id = *result;
            } else {
                connection.SendLogMessage(worker::LogLevel::kError, "Creating Entity", result.GetErrorMessage());
                std::cout << "[local] Failed to create entity: " << result.GetErrorMessage() << std::endl;
                std::terminate();
            }
        }
    });
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

    if (is_connected) {
        std::cout << "[local] Connected successfully to SpatialOS, listening to ops... " << std::endl;
    }

    //Create entity test object
    for (int i = 0; i < 1000; i++) {
        CreateSpecialEntity(connection, view, SpecialTest(444, "Ben", "Moser"));
    }

    CreateDummyEntity(connection, view, "J0SH");
    CreateExternalEntity(connection, view, "EXT3RNAL");

    //Update variables 
    special::Health::Update health_update;
    special::Name::Update name_update;

    //Random number between lower and upper bounds, inclusive
    auto random_health = [](int lower, int upper) {
        return rand()%(upper - lower + 1) + lower;
    };

    std::cout << "[local] Starting game loopie!" << std::endl;
    
    //This is the game loop :)
    while (is_connected) {
        //dispatcher.Process(connection.GetOpList(kGetOpListTimeoutInMilliseconds));
        //The ops list is so the connection doesn't time out
        auto ops = connection.GetOpList(kGetOpListTimeoutInMilliseconds);

        //Process ops so entities and components get added automatically
        view.Process(ops);

        std::cout << "About to process entities..." << std::endl;
        //Now let's iterate over all entities and update their components
        for (auto it = view.Entities.begin(); it != view.Entities.end(); it++) {
            auto entity_id = it -> first;
            std::cout << "Updating entity ID " << entity_id << std::endl;

            //Make random values:
            health_update.set_remaining(random_health(0, 100));
            name_update.set_first(get_random_characters(5));
            name_update.set_last(get_random_characters(8));

            //Send updates to SpatialOS!
            connection.SendComponentUpdate<special::Health>(entity_id, health_update);
            connection.SendComponentUpdate<special::Name>(entity_id, name_update);

            std::cout << "End Entity update" << std::endl;
        }

        std::cout << "Ending game loop" << std::endl;
        //Now go to sleep for a bit to avoid excess changes
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    return ErrorExitStatus;
}

