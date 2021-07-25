#include "MapReduceFramework.h"
#include <atomic>
#include <pthread.h>
#include <iostream>
#include "Barrier.h"

using namespace std;
#define SYSTEM_ERR "system error: "
#define THREAD_CREATE_ERR_MSG "pthread_create error"
#define MUTEX_DESTROY_ERR_MSG "mutex_destroy error"
#define THREAD_JOIN_ERR_MSG "pthread_join error"
#define MUTEX_LOCK_ERR_MSG "mutex_lock error"
#define MUTEX_UNLOCK_ERR_MSG "mutex_unlock error"
struct JobContext;

void handleSystemErr(const string &msg) {
    cerr << SYSTEM_ERR << msg << endl;
    exit(EXIT_FAILURE);
}

/**
 * struct which include all the shared data between the threads
 */
struct ThreadContext {
    JobContext &_jc;
    vector<IntermediatePair> _myIntermediateOutput;
    pthread_mutex_t mutexThreadVec{};

    //todo problem with initializing this like that(jc allocates)?
    explicit ThreadContext(JobContext &jc) : _jc(jc) {
        pthread_mutex_init(&mutexThreadVec, nullptr);
    }
};

/**
 * structs which includes all the parameters which are relevant to the job, the pointer to this struct can be cast to JobHandle
 */
struct JobContext {
    int _threadsSize;
    pthread_t *_threads;
//    int runningThreads;
    ThreadContext **_threadContexts; //todo pointer to pointer must??
    map<K2 *, vector<V2 *>, K2PointerComp> _intermediateMap;
    vector<K2 *> uniqueKeys;
    const MapReduceClient &_client;
    const InputVec &_inputVec;
    OutputVec &_outputVec;
    bool isJoined; // eliminate unwanted behavior in the waitFor
    pthread_mutex_t mutexInputVec{};
    pthread_mutex_t mutexOutputVec{};
    pthread_mutex_t mutexShuffleStage{};
    Barrier barrier;
    std::atomic<int> *inputCounter; // current position on the  inputVector
    std::atomic<int> *runningThreads;
    std::atomic<int> *processedKeysMapPhase; // number of keys that have been processed in the map phase already
    std::atomic<int> *processedKeysReducePhase; // the number of keys already have been processed bt the map phase
    std::atomic<int> *takenFromInterMap; // the number of keys already have been taken to process to reduce phase
    std::atomic<int> *totalInterMapKeys; // the number of keys in the map in the shuffle phase
    std::atomic<int> *totalK1Keys;
    std::atomic<int> *atomicStage;
    std::atomic<int> *shuffled; // the number of keys we already shuffeld
    std::atomic<int> *toShuffle; // total keys to shuffle
    std::atomic<int> *finishMapPhaseCounter; //counter for threads that finished map phase

    JobContext(const MapReduceClient &client, const InputVec &inputVec, OutputVec &outputVec, int multiThreadLevel)
            : _threadsSize(multiThreadLevel), _client(client), _inputVec(inputVec), _outputVec(outputVec),
            barrier(multiThreadLevel) {
        _threads = new pthread_t[multiThreadLevel]; // TODO free
        _threadContexts = new ThreadContext *[multiThreadLevel]; // TODO free
        isJoined = false;
        pthread_mutex_init(&mutexOutputVec, nullptr);
        pthread_mutex_init(&mutexShuffleStage, nullptr);
        pthread_mutex_init(&mutexInputVec, nullptr);
        inputCounter = new atomic<int>(0);
        processedKeysMapPhase = new atomic<int>(0);
        processedKeysReducePhase = new atomic<int>(0);
        totalK1Keys = new atomic<int>(0);
        totalInterMapKeys = new atomic<int>(0);
        takenFromInterMap = new atomic<int>(0);
        atomicStage = new atomic<int>(0);
        shuffled = new atomic<int>(0);
        toShuffle = new atomic<int>(0);
        finishMapPhaseCounter = new atomic<int>(0);
        runningThreads = new atomic<int>(0);
        totalK1Keys->store((int) inputVec.size());
        for (int i = 0; i < _threadsSize; ++i) {
            _threadContexts[i] = new ThreadContext(*this);//TODO free
        }
    }
};

/**
 * iterating on threads intemdiatevectors and add them to the map
 * @param context thread context
 * @param civ vector of counters in each intermediatevector of each thread
 */
void shuffleHelper(void *context, vector<int> &civ) {
    auto *tc = (ThreadContext *) context;
    int sysOut;
    for (int i = 0; i < tc->_jc._threadsSize - 1; ++i) {
        sysOut = pthread_mutex_lock(&(tc->_jc._threadContexts[i]->mutexThreadVec));
        if (sysOut != EXIT_SUCCESS) {
            handleSystemErr(MUTEX_LOCK_ERR_MSG);
        }
        auto curVector = tc->_jc._threadContexts[i]->_myIntermediateOutput;
        for (size_t j = civ[i]; j < curVector.size(); j++) {

            tc->_jc._intermediateMap[curVector[j].first].push_back(curVector[j].second);
            civ[i]++;
            (*(tc->_jc.shuffled))++;

        }
        sysOut = pthread_mutex_unlock(&(tc->_jc._threadContexts[i]->mutexThreadVec));
        if (sysOut != EXIT_SUCCESS) {
            handleSystemErr(MUTEX_UNLOCK_ERR_MSG);
        }
    }
}

void *shuffle(void *context) {
    auto *tc = (ThreadContext *) context;
    vector<int> countersInVec(tc->_jc._threadsSize - 1, 0);
    tc->_jc.atomicStage->store(MAP_STAGE);
    (*(tc->_jc.runningThreads))++;
    int oldNeededShuffle=0;
    while (true) {
        if (tc->_jc.finishMapPhaseCounter->load() == tc->_jc._threadsSize - 1) {
            tc->_jc.atomicStage->store(SHUFFLE_STAGE);
            shuffleHelper(tc, countersInVec);
            for (auto &it:tc->_jc._intermediateMap) { //making unique keys vector
                tc->_jc.uniqueKeys.push_back(it.first);
            }
            tc->_jc.totalInterMapKeys->store((int) tc->_jc._intermediateMap.size());
            tc->_jc.atomicStage->store(REDUCE_STAGE);
            tc->_jc.barrier.barrier();
            break;
        } else {
            if(oldNeededShuffle != tc->_jc.toShuffle->load()) {
                shuffleHelper(tc, countersInVec);
                oldNeededShuffle=tc->_jc.toShuffle->load();
            }
        }
    }
    return nullptr;
}

void *MapReduceExecution(void *context) {
    auto *tc = (ThreadContext *) context;
    int inputVecSize = (int) (tc->_jc._inputVec).size();
    (*(tc->_jc.runningThreads))++;
    while (true) {
        int old_value = (*(tc->_jc.inputCounter))++;
        if (old_value >= inputVecSize) { // thread out of bound
            break;
        } else {  // thread can keep running

            tc->_jc._client.map(tc->_jc._inputVec[old_value].first, tc->_jc._inputVec[old_value].second, tc);
            (*(tc->_jc.processedKeysMapPhase))++;
        }
    }
//    #############  waiting for shuffle ########################

    (*(tc->_jc.finishMapPhaseCounter))++;//after
    tc->_jc.barrier.barrier();

//           ############## reduce ##############

    int uniqueKeysSize = (int) tc->_jc.uniqueKeys.size();
    while (true) {
        int old_value = (*(tc->_jc.takenFromInterMap))++;
        if (old_value >= uniqueKeysSize) {
            break;
        } else {
            auto key = tc->_jc.uniqueKeys[old_value];
            auto values = tc->_jc._intermediateMap[key];

            tc->_jc._client.reduce(key, values, tc);
            (*(tc->_jc.processedKeysReducePhase))++;
        }
    }
    return nullptr;
}

/**
 *
 * @param client The implementation of MapReduceClient class, the task that the framework should run.
 * @param inputVec a vector of type vector<pair<K2*,V2*>>, the input elements
 * @param outputVec a vector of vector<pair<K3*,V3*>>, to which the output elements will be added before returning.
 * @param multiThreadLevel the number of worker threads to be used for running the algorithm
 * @return JobHandle object
 */
JobHandle
startMapReduceJob(const MapReduceClient &client, const InputVec &inputVec, OutputVec &outputVec, int multiThreadLevel) {
    auto *job = new JobContext(client, inputVec, outputVec, multiThreadLevel);
    int sysOut;
    for (int i = 0; i < multiThreadLevel - 1; i++) {
        sysOut = (pthread_create(&(job->_threads[i]), nullptr, MapReduceExecution,
                                 (job->_threadContexts[i]))); //,THREAD_CREATE_ERR_MSG)
        if (sysOut != EXIT_SUCCESS) {
            handleSystemErr(THREAD_CREATE_ERR_MSG);
        }
    }
    sysOut = pthread_create(&(job->_threads[multiThreadLevel - 1]), nullptr, shuffle,
                            job->_threadContexts[multiThreadLevel - 1]);//, THREAD_CREATE_ERR_MSG)
    if (sysOut != EXIT_SUCCESS) {
        handleSystemErr(THREAD_CREATE_ERR_MSG);
    }
    return job;
}

/**
 * gets the jobHandle returned by startMapReduceJob and waits until it is finished
 * @param job the jobHandle object
 */
void waitForJob(JobHandle job) {
    auto *context = (JobContext *) job;
    int sysOut;
    for (int i = 0; i < context->_threadsSize; ++i) {
        sysOut = pthread_join((context->_threads)[i], nullptr);
        if (sysOut != EXIT_SUCCESS) {
            handleSystemErr(THREAD_JOIN_ERR_MSG);
        }
    }
    while (context->runningThreads->load() < context->_threadsSize);
    context->isJoined = true;
}

/**
 * updates the jobState into the new one
 * @param job our jobHandle object
 * @param state the new atomicStage we want to move into
 */
void getJobState(JobHandle job, JobState *state) {
    auto *context = (JobContext *) job;
    state->stage = (stage_t) context->atomicStage->load(std::memory_order_relaxed);
    float numerator = 0, denominator = 1;
    switch (state->stage) {
        case UNDEFINED_STAGE:
            numerator = 0;
            break;
        case MAP_STAGE:
            numerator = (float) context->processedKeysMapPhase->load();
            denominator = (float) context->totalK1Keys->load();
            break;
        case SHUFFLE_STAGE:
            numerator = (float) context->shuffled->load();
            denominator = (float) context->toShuffle->load();
            break;
        case REDUCE_STAGE:
            numerator = (float) context->processedKeysReducePhase->load();
            denominator = (float) context->totalInterMapKeys->load();
            break;
    }
    state->percentage = (numerator / denominator) * 100;
}

/**
 * releasing all resources of a job. must check if the job really finished before -- basically free function
 * @param job our jobHandle object
 */
void closeJobHandle(JobHandle job) {
    auto *context = (JobContext *) job;
    if (!context->isJoined) {
        waitForJob(job);
    } // wont let us procceed unless the job is finished
    int sysOut;
    sysOut = pthread_mutex_destroy(&context->mutexInputVec);
    if (sysOut != EXIT_SUCCESS) {
        handleSystemErr(MUTEX_DESTROY_ERR_MSG);
    }
    sysOut = pthread_mutex_destroy(&context->mutexOutputVec);
    if (sysOut != EXIT_SUCCESS) {
        handleSystemErr(MUTEX_DESTROY_ERR_MSG);
    }
    sysOut = pthread_mutex_destroy(&context->mutexShuffleStage);
    if (sysOut != EXIT_SUCCESS) {
        handleSystemErr(MUTEX_DESTROY_ERR_MSG);
    }
    delete context->inputCounter;
    delete context->processedKeysMapPhase;
    delete context->processedKeysReducePhase;
    delete context->takenFromInterMap;
    delete context->shuffled;
    delete context->toShuffle;
    delete context->finishMapPhaseCounter;
    delete context->totalK1Keys;
    delete context->totalInterMapKeys;
    delete context->atomicStage;
    for (int i = 0; i < context->_threadsSize; ++i) {
        sysOut = pthread_mutex_destroy(&context->_threadContexts[i]->mutexThreadVec);
        if (sysOut != EXIT_SUCCESS) {
            handleSystemErr(MUTEX_DESTROY_ERR_MSG);
        }
        delete context->_threadContexts[i];
    }
    delete[] context->_threadContexts;
    delete[] context->_threads;
    delete (JobContext *) job;
}

/**
 * produces a (K2,V2) pairs.
 * @param key the key we are updating
 * @param value the value we want to update
 * @param context have pointers into the frameworks variables and ds == meaning shared data
 */
void emit2(K2 *key, V2 *value, void *context) {
    auto *tc = (ThreadContext *) context;
    int sysOut = pthread_mutex_lock(&(tc->mutexThreadVec));
    if (sysOut != EXIT_SUCCESS) {
        handleSystemErr(MUTEX_LOCK_ERR_MSG);
    }

    tc->_myIntermediateOutput.emplace_back(IntermediatePair(key, value));
    (*(tc->_jc.toShuffle))++;

    sysOut = pthread_mutex_unlock(&(tc->mutexThreadVec));
    if (sysOut != EXIT_SUCCESS) {
        handleSystemErr(MUTEX_UNLOCK_ERR_MSG);
    }
}

/**
  * produces a (K3,V3) pairs.
 * @param key the key we are updating
 * @param value the value we want to update
 * @param context have pointers into the frameworks variables and ds == meaning shared data
 */
void emit3(K3 *key, V3 *value, void *context) {
    auto *tc = (ThreadContext *) context;
    int sysOut = pthread_mutex_lock(&(tc->_jc.mutexOutputVec));
    if (sysOut != EXIT_SUCCESS) {
        handleSystemErr(MUTEX_LOCK_ERR_MSG);
    }

    tc->_jc._outputVec.push_back(pair<K3 *, V3 *>(key, value));

    sysOut = pthread_mutex_unlock(&(tc->_jc.mutexOutputVec));
    if (sysOut != EXIT_SUCCESS) {
        handleSystemErr(MUTEX_UNLOCK_ERR_MSG);
    }
}

