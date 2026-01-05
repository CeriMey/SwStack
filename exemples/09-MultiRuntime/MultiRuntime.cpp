#include <iostream>
#include "SwCoreApplication.h"
#include "SwEventLoop.h"
#include "SwTimer.h"

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#define PID() GetCurrentProcessId()
#else
#include <unistd.h>
#define PID() getpid()
#endif

void task1() {
    SwEventLoop::swsleep(1500);
    std::cout << "LOAD % = " << SwCoreApplication::instance()->getLoadPercentage()<< std::endl;
}

// Tâche lente : exécutée toutes les 3 secondes
void task2() {
    std::cout << "[Task2] Entering slow task (3 seconds delay). PID = " << PID() << std::endl;
    SwEventLoop::swsleep(3000); // Pause de 3 secondes
    std::cout << "[Task2] Exiting slow task. PID = " << PID() << std::endl;
}

// Tâche lente : exécutée toutes les 5 secondes
void task3() {
    std::cout << "[Task3] Entering slow task (5 seconds delay). PID = " << PID() << std::endl;
    SwEventLoop::swsleep(3000); // Pause de 3 secondes dans la tâche
    std::cout << "[Task3] Debug: Paused inside task. PID = " << PID() << std::endl;
}

// Callback du timer
void onTimerTimeout() {
    static int timerCount = 0;
    ++timerCount;
    std::cout << "[Timer] Timer triggered! Timer Count = " << timerCount << ", PID = " << PID() << std::endl;
}

static void blockingSubtask() {
    std::cout << "[Blocker] Entering infinite loop subtask. PID = " << PID() << std::endl;
    volatile unsigned long long spin = 0;
    while (true) {
        ++spin;
    }
}

static void startBlockingSubtask() {
    std::cout << "[Blocker] Scheduling infinite loop subtask..." << std::endl;
    SwCoreApplication::instance()->postEvent(blockingSubtask);
}

int main() {
    SwCoreApplication app;

    // Installation des tâches dans la boucle d'événements
    SwEventLoop::installRuntime(task1);
    SwEventLoop::installSlowRuntime(3000, task2); // 3 secondes
    SwEventLoop::installSlowRuntime(5000, task3); // 5 secondes

    // Démarrage d'un timer pour exécuter un événement toutes les 3 secondes
    SwTimer timer;
    timer.setInterval(1500); // Intervalle de 1 seconde
    SwObject::connect(&timer, &SwTimer::timeout, [](){
        onTimerTimeout();
    });
    timer.start();

    std::cout << "[Main] Application started. Running event loop... PID = " << PID() << std::endl;
    app.activeWatchDog();

    SwTimer::singleShot(2000, []() {
        startBlockingSubtask();
    });
    return app.exec();
}
