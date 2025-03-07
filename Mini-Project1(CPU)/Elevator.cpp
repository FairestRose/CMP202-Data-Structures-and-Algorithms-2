// CMP202
// Elevator System Simulation - Mini Project 1 for DSA2
// Snow White 
// Semaphores, Atomics, Mutex, Deadlock and more 


#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <semaphore>
#include <atomic>
#include <chrono>
#include <map>
#include <random> 
#include <semaphore>
#include <condition_variable>
#include <functional> // Include for std::function




using namespace std;

// Constants to define the elevator system's properties
constexpr int TOTAL_LEVELS = 10; // Total number of floors in the building
constexpr int MAX_CAPACITY = 3; // Maximum number of passengers the elevator can hold
constexpr int MOVE_DURATION = 2000; // Time in milliseconds it takes the elevator to move between floors
constexpr int STOP_DURATION = 500; // Time in milliseconds for the elevator to stop at a floor (not used here)
constexpr int THRESHOLD = 3; // Define the threshold for stopping the program after a certain number of passengers reach their destinations

// Global flag to indicate when to exit the main loop

// Global synchronization primitives and variables
// 
// Semaphore to limit elevator capacity -- Todo Task 2
std::atomic<int> currentLevel(0); // Current floor of the elevator, starting at ground floor (0)
std::atomic<bool> movingUp(true); // Direction of elevator movement, initially set to move up 

std::atomic<int> passengersInElevator(0); // Number of passengers currently in the elevator
std::mutex coutMutex; // Mutex for synchronizing access to console output
std::mutex elevatorMutex; // Mutex for synchronizing access to elevator-specific operations
std::counting_semaphore<MAX_CAPACITY> elevatorCapacity(MAX_CAPACITY);

std::mutex waitingMutex; // Mutex for synchronizing access to waiting passengers
std::condition_variable waitingCV; // Condition variable for waiting passengers
std::condition_variable ridingCV; // Condition variable for passengers inside the elevator

// Global synchronization primitives and variables
std::atomic<int> passengersReachedDestination(0); // Counter for passengers who have reached their destinations

// Data structures for tracking passengers
vector<pair<int, int>> passengersInsideElevator; // Vector to keep track of passengers inside the elevator (ID, destination floor)
std::mutex passengersInsideElevatorMutex; // Mutex for synchronizing access to the passengersInsideElevator vector
map<int, vector<pair<int, int>>> pickupRequests; // Map to hold pickup requests per floor (floor number, vector of (ID, destination floor))
std::mutex pickupRequestsMutex; // Mutex for synchronizing access to pickup requests
map<int, vector<pair<int, int>>> deliveredPassengers; // Map to hold delivered passengers per floor
std::mutex deliveredPassengersMutex; // Mutex for synchronizing access to deliveredPassengers map

// Mutex to protect access to the currentLevel variable
std::mutex currentLevelMutex;

// Function to clear the console. The implementation depends on the operating system
void clearConsole() {
#ifdef _WIN32
    system("cls"); // Clears console on Windows systems
#else
    system("clear"); // Clears console on Unix/Linux systems
#endif
}
// Function to print the current state of the building and elevator.
void printBuilding() {
    lock_guard<mutex> lock(coutMutex); // Ensures exclusive access to std::cout.
    clearConsole(); // Clears the console for a fresh display of the building state.
    for (int i = TOTAL_LEVELS - 1; i >= 0; --i) { // Iterates over each floor from top to bottom.
        cout << "Floor " << i << ": ";
        if (i == currentLevel) { // Checks if the elevator is at the current floor.
            // Displays the elevator's status, direction, and passenger count.
            cout << "[E: ";
            cout << (movingUp ? "Up" : "Down") << ", P: " << passengersInElevator.load() << "/" << MAX_CAPACITY << " ";
            for (size_t index = 0; index < passengersInsideElevator.size(); ++index) { // Iterates over passengers in the elevator.
                const auto& passenger = passengersInsideElevator[index];
                // Displays passenger ID and destination floor with color coding.
                cout << "\033[35m" << passenger.first << "t" << passenger.second << "\033[0m";
                if (index < passengersInsideElevator.size() - 1) {
                    cout << ", "; // Comma between passengers for readability.
                }
            }
            cout << "] ";
        }
        else {
            // Fills the line for floors without the elevator for alignment.
            cout << "                                     ";
        }

        // Delivered passengers
        auto& delivered = deliveredPassengers[i];
        for (auto& passenger : delivered) { // Iterates over delivered passengers at the current floor.
            // Displays delivered passenger ID and destination floor with color coding.
            cout << "\033[32m" << passenger.first << "t" << passenger.second << "\033[0m ";
        }

        // Waiting passengers
        auto& waiting = pickupRequests[i];
        for (auto& passenger : waiting) {
            cout << "  \033[31m" << passenger.first << "t" << passenger.second << "\033[0m ";
        }

        cout << endl; // New line for the next floor.

        // Notify waiting passengers on this floor that the elevator has arrived
        waitingCV.notify_all();
    }
}

// Passenger class definition
class Passenger {
public:
    int id; // Unique identifier for the passenger.
    int startFloor; // Floor where the passenger will board the elevator.
    int destinationFloor; // Passenger's destination floor.
    std::function<void(int, int, const std::chrono::time_point<std::chrono::steady_clock>&)> leaveElevatorFunc; // Function reference to leaveElevator

    // Constructor to initialize a passenger with an ID, start floor, destination floor, and leaveElevator function.
    Passenger(int id, int start, int destination, std::function<void(int, int, const std::chrono::time_point<std::chrono::steady_clock>&)> leaveFunc) : id(id), startFloor(start), destinationFloor(destination), leaveElevatorFunc(leaveFunc) {}

    // Function operator to simulate passenger behavior.
    void operator()() {
        {
            lock_guard<mutex> lockP(pickupRequestsMutex); // Protects addition of pickup request.
            pickupRequests[startFloor].push_back(pair(id, destinationFloor)); // Adds pickup request for this passenger.
        }
        // Wait for the elevator to arrive at the start floor.
        while (currentLevel != startFloor) {
            this_thread::sleep_for(chrono::milliseconds(50)); // Sleeps to reduce CPU usage while waiting.
        }

        // Waits until there's space in the elevator, simulating boarding.
        elevatorCapacity.acquire(); // Acquire a space in the elevator.

        passengersInElevator++; // Increments the passenger count in the elevator.

        {
            lock_guard<mutex> lock(elevatorMutex); // Protects elevator state changes.
            printBuilding(); // Updates the building display.
            cout << "Passenger " << id << " boarded at floor " << startFloor << " and is going to floor " << destinationFloor << endl;
        }

        // Locks for dealing with pickup and inside-elevator passenger lists without causing deadlocks.
        std::unique_lock<std::mutex> lk1(pickupRequestsMutex, std::defer_lock);
        std::unique_lock<std::mutex> lk2(passengersInsideElevatorMutex, std::defer_lock);
        std::lock(lk1, lk2); // Locks both mutexes at once to prevent deadlock.

        // Finds this passenger in the pickupRequests to remove them, indicating they're now inside the elevator.
        auto itp = std::find_if(pickupRequests[startFloor].begin(), pickupRequests[startFloor].end(), [=](const auto& pair) {
            return pair.first == id;
            });

        if (itp != pickupRequests[startFloor].end()) {
            pickupRequests[startFloor].erase(itp); // Removes passenger from the pickupRequests.
            passengersInsideElevator.push_back(pair(id, destinationFloor)); // Adds passenger to the inside-elevator list.
            printBuilding(); // Updates the building display to reflect the new state.
        }

        // Unlocks the mutexes manually.
        lk1.unlock();
        lk2.unlock();

        // Waits for the elevator to reach the destination floor.
        while (currentLevel != destinationFloor) {
            this_thread::sleep_for(chrono::milliseconds(100)); // Sleeps to reduce CPU usage while waiting.
        }

        // Simulates leaving the elevator, making space for new passengers.
        leaveElevatorFunc(id, destinationFloor, std::chrono::steady_clock::now());

        // Simulates leaving the elevator, making space for new passengers.
        elevatorCapacity.release(); // Release a space in the elevator.
        passengersInElevator--; // Decrements the passenger count in the elevator.

        // Locks for dealing with inside-elevator and delivered passenger lists to update states.
        passengersInsideElevatorMutex.lock();
        deliveredPassengersMutex.lock();

        // Finds this passenger in the passengersInsideElevator to remove them, indicating they've arrived.
        auto it = std::find_if(passengersInsideElevator.begin(), passengersInsideElevator.end(), [=](const auto& pair) {
            return pair.first == id;
            });

        if (it != passengersInsideElevator.end()) {
            passengersInsideElevator.erase(it); // Removes passenger from the inside-elevator list.
            deliveredPassengers[destinationFloor].push_back(pair(id, destinationFloor)); // Adds passenger to the delivered list.
            printBuilding(); // Updates the building display to reflect the new state.
        }

        // Unlocks the mutexes.
        passengersInsideElevatorMutex.unlock();
        deliveredPassengersMutex.unlock();
    }
};






// The elevator function simulates the movement of the elevator through the building.
void elevator() {
    // Infinite loop to keep the elevator moving.
    while (true) {
        // Waits for MOVE_DURATION, simulating floor-to-floor movement.
        this_thread::sleep_for(chrono::milliseconds(MOVE_DURATION));

        // Acquire lock to protect access to currentLevel
        std::lock_guard<std::mutex> lock(currentLevelMutex);

        // Moves the elevator up or down based on the direction.
        currentLevel += movingUp ? 1 : -1;

        // Reverses direction at the top or bottom floor.
        if (currentLevel == 0 || currentLevel == TOTAL_LEVELS - 1)
            movingUp = !movingUp;

        // Releases the lock


        {
            // Protects elevator state changes.
            lock_guard<mutex> lock(elevatorMutex);
            // Updates the building display after each move.
            printBuilding();
        }

        // Notify passengers inside the elevator about the change in floor.
        ridingCV.notify_all();

        // Wait for a short duration (simulating the time the elevator stays at a floor).
        this_thread::sleep_for(chrono::milliseconds(STOP_DURATION));
    }
}



// Function to simulate leaving the elevator, making space for new passengers.
void leaveElevator(int passengerId, int destinationFloor, const std::chrono::time_point<std::chrono::steady_clock>& startTime) {
    // Simulates leaving the elevator, making space for new passengers.
    {
        // Acquire lock to protect access to passengersReachedDestination
        std::lock_guard<std::mutex> lock(deliveredPassengersMutex);
        passengersReachedDestination++; // Increment counter for passengers who have reached their destinations
    }

    // Locks for dealing with inside-elevator and delivered passenger lists to update states.
    passengersInsideElevatorMutex.lock();
    deliveredPassengersMutex.lock();

    // Finds this passenger in the passengersInsideElevator to remove them, indicating they've arrived.
    auto it = std::find_if(passengersInsideElevator.begin(), passengersInsideElevator.end(), [=](const auto& pair) {
        return pair.first == passengerId;
        });

    if (it != passengersInsideElevator.end()) {
        passengersInsideElevator.erase(it); // Removes passenger from the inside-elevator list.
        deliveredPassengers[destinationFloor].push_back(pair(passengerId, destinationFloor)); // Adds passenger to the delivered list.
        printBuilding(); // Updates the building display to reflect the new state.
    }

    // Unlocks the mutexes.
    passengersInsideElevatorMutex.unlock();
    deliveredPassengersMutex.unlock();

    // Check if the number of passengers who have reached their destinations exceeds a threshold
    if (passengersReachedDestination >= THRESHOLD) {
        // Record the end time.
        auto endTime = std::chrono::steady_clock::now();

        // Calculate the elapsed time in seconds.
        auto elapsedTime = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime);

        // Extract minutes and remaining seconds from the total elapsed time.
        auto minutes = std::chrono::duration_cast<std::chrono::minutes>(elapsedTime);
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsedTime - minutes);

        // Print the total time taken by the elevator.
        std::cout << "Elevator finished in " << minutes.count() << " minutes and " << seconds.count() << " seconds." << std::endl;

        // If the threshold is reached, print a message and exit the program
        cout << "Threshold reached. Exiting program." << endl;
        exit(0); // Terminate the program 
    }
}



int main() {
    // Stores threads for each passenger.
    vector<thread> threads;

    // Thread for the elevator's continuous operation.
    thread elevatorThread(elevator);

    // Record the start time.
    auto startTime = chrono::steady_clock::now();

    // Non-deterministic random number generator.
    std::random_device rd;

    // Random number generator using Mersenne Twister algorithm.
    std::mt19937 rng(rd());

    // Distribution for floor numbers.
    std::uniform_int_distribution<int> uniStart(0, TOTAL_LEVELS - 1);

    // Function reference to leaveElevator function
    auto leaveElevatorFunc = std::bind(&leaveElevator, std::placeholders::_1, std::placeholders::_2);

    // Main loop for continuously generating passengers.
    while (true) {
        // Generate a new passenger with random start and destination floors.
        int startFloor = uniStart(rng);

  
        // Distribution for destination floors, ensuring it's within the valid range of floors.
        std::uniform_int_distribution<int> uniDest(0, TOTAL_LEVELS - 1);

        int destinationFloor;

        // Ensure destination floor is different from the start floor.
        do {
            destinationFloor = uniDest(rng);
        } while (destinationFloor == startFloor);

        threads.emplace_back(Passenger(threads.size(), startFloor, destinationFloor, std::bind(&leaveElevator, std::placeholders::_1, std::placeholders::_2, startTime)));


        // Wait for a random duration before generating the next passenger.
        std::this_thread::sleep_for(std::chrono::seconds(1)); // Adjust the duration as needed.

        // Check if the threshold is reached.
        if (passengersReachedDestination >= THRESHOLD) {
            // If the threshold is reached, print a message and exit the loop.
            cout << "Threshold reached. Exiting program." << endl;
            break;
        }
    }

    // Record the end time.
    auto endTime = chrono::steady_clock::now();

    // Calculate the elapsed time in seconds.
    auto elapsedTime = chrono::duration_cast<chrono::seconds>(endTime - startTime);

    // Extract minutes and remaining seconds from the total elapsed time.
    auto minutes = chrono::duration_cast<chrono::minutes>(elapsedTime);
    auto seconds = chrono::duration_cast<chrono::seconds>(elapsedTime - minutes);

    // Print the total time taken by the elevator.
    cout << "Elevator finished in " << minutes.count() << " minutes and " << seconds.count() << " seconds." << endl;

    // Allows the elevator thread to run independently of the main thread.
    elevatorThread.detach();

    return 0;
}

