#include <iostream>
#include <sys/shm.h> // shared memory import
#include <sys/wait.h> // process syncing
#include <unistd.h> // unix standard lib
#include <sys/sem.h> // semaphore import
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <vector>

using namespace std;

union semun {
    int val; // semaphore counter
    struct semid_ds *buf; // pointer to metadata of semaphore
    unsigned short *array; // array of semaphore count values
};

struct SharedData {
    char rubric[5][10];
    char current_exam[5];
    int questions_marked[5];
    int current_student_num;
    bool finished;
    int reviewed_rubric[100];
};

double random_delay(double min, double max) { // random delay function, used for both reviewing/correcting rubric delay and marking delay
    return min + (rand() / (double)RAND_MAX) * (max - min); // 0.5 + some random number mulitpled by (max - min)
}

void sem_wait(int semid, int sem_num){
    struct sembuf op;
    op.sem_num = sem_num; // which semaphore to operate on, 0 = rubric, 1 = question marking, 2 = exam loading
    op.sem_op = -1; // if <0, blocks/waits 
    op.sem_flg = 0; // if counter == 0, block
    semop(semid, &op, 1); // execute the semaphore op
}

void sem_signal(int semid, int sem_num){
    struct sembuf op; 
    op.sem_num = sem_num;
    op.sem_op = 1; // unlocked
    op.sem_flg = 0; // if counter == 0, block
    semop(semid, &op, 1); // execute the semaphore op
}

void load_rubric(SharedData* data) { // reads rubric.txt file and loads into shared memory
    ifstream file("../inputs/rubric/rubric.txt");
    if (!file.is_open()) { // if file couldn't be open
        cerr << "error: could not open rubric.txt" << endl;
        exit(1);
    }
    
    for (int i = 0; i < 5; i++) {
        file.getline(data->rubric[i], 10); // parse through every line
    }
    
    file.close(); // close the file
}

void save_rubric(SharedData* data) {
    ofstream file("../inputs/rubric/rubric.txt"); // get rubric file
    if (!file.is_open()) { // if couldnt save to rubric
        cerr << "error: could not save rubric.txt" << endl;
        return;
    }
    
    for (int i = 0; i < 5; i++) { // parse through each line in the rubric
        file << data->rubric[i]; // change rubric
        if (i < 4) file << endl; 
    }
    
    file.close();
}

void load_exam(SharedData* data, int student_num) { // pass in shared memory and student number to load in their exams
    data->current_student_num = student_num;
    for (int i = 0; i < 5; i++) {
        data->questions_marked[i] = 0; // parse through the lines of the exam
    }
    for (int i = 0; i < 100; i++) {
        data->reviewed_rubric[i] = 0; // Reset reviewed flags for new exam
    }
    cout << "loaded exam for student " << student_num << endl;
}

int get_next_exam(int current) { // get next exam in shared memory, starting from the current exam being viewed
    ifstream file("../inputs/exams/exam_" + to_string(current + 1).insert(0, 4 - to_string(current + 1).length(), '0') + ".txt"); // pass in next exam file name
    if (file.is_open()) { // open file
        int student_num;
        file >> student_num; // extract student number for new exam file
        file.close(); // close file
        return student_num; // return student number
    }
    return -1; // if failed to get next exam, return -1
}

void review_rubric(int ta_id, SharedData* data, int semid) { // TA reviews the rubric
    cout << "TA " << ta_id << ": reviewing rubric" << endl;
    
    for (int i = 0; i < 5; i++) { // for every exercise in the rubric
        double delay = random_delay(0.5, 1.0); // get the delay
        usleep(delay * 1000000); // function wide sleep
        
        if (rand() % 5 == 0) {
            sem_wait(semid, 0); // semaphore waits for signal to get access to correct rubric
            cout << "TA " << ta_id << ": correcting rubric question " << (i + 1) << endl;
            char current = data->rubric[i][2];
            data->rubric[i][2] = current + 1;
            cout << "TA " << ta_id << ": rubric question " << (i + 1) << " changed from '" << current << "' to '" << data->rubric[i][2] << "'" << endl;
            save_rubric(data);
            sem_signal(semid, 0); // semaphore frees signal
        }
    }
}

void mark_questions(int ta_id, SharedData* data, int semid) { // TA marking a exercise
    while (!data->finished) { // while we still have students to mark
        // Check if this TA needs to review rubric for current exam
        if (data->reviewed_rubric[ta_id - 1] == 0) {
            review_rubric(ta_id, data, semid);
            data->reviewed_rubric[ta_id - 1] = 1; // Mark as reviewed
        }
        
        int question = -1;
        int current_exam_num; // Store exam number locally to avoid race condition
        
        sem_wait(semid, 1);
        for (int i = 0; i < 5; i++) { // parse through all of the exercises
            if (data->questions_marked[i] == 0) { // if a question isnt marked
                question = i;
                cout << "TA " << ta_id << ": claiming question " << (question + 1) << endl;
                data->questions_marked[i] = 1; // if a question is marked
                break;
            }
        }
        current_exam_num = data->current_student_num; // Capture exam number while protected
        sem_signal(semid, 1);
        
        if (question == -1) { // if all questions were marked for the current exam
            sem_wait(semid, 2); // wait for semaphore signal
            if (data->current_student_num == 9999) {
                cout << "TA " << ta_id << ": setting finished flag for student 9999" << endl;
                data->finished = true;
                sem_signal(semid, 2);
                return;
            }
            int next_student = get_next_exam(data->current_student_num);
            if (next_student == -1){
                cout << "TA " << ta_id << ": no more exams, setting finished flag" << endl;
                data->finished = true;
                sem_signal(semid, 2);
                return;
            }
            load_exam(data, next_student);
            sem_signal(semid, 2); // release semaphore signla
            continue;
        }
        
        cout << "TA " << ta_id << ": marking exam " << current_exam_num << " question " << (question + 1) << endl;
        double delay = random_delay(1.0, 2.0); // marking delay
        usleep(delay * 1000000); // function wide sleep
        cout << "TA " << ta_id << ": finished marking exam " << current_exam_num << " question " << (question + 1) << endl;
    }
}

void ta_process(int ta_id, int shmid, int semid) { // function to create TA process
    srand(time(NULL) + ta_id); // random seed for TA
    
    SharedData* data = (SharedData*)shmat(shmid, NULL, 0); // attach to shared memory
    
    mark_questions(ta_id, data, semid); // mark questions
    
    shmdt(data); // detach from shared mem
    exit(0); // terminate process
}

int main(int argc, char* argv[]) {
    if (argc != 2) { // arguement validation
        cerr << "usage: " << argv[0] << " <num_tas>" << endl;
        return 1;
    }
    
    int num_tas = atoi(argv[1]); // convert string into integer
    if (num_tas < 2) { // validate we have atleast TWO TA's
        cerr << "must have at least 2 TAs" << endl;
        return 1;
    }
    
    int shmid = shmget(IPC_PRIVATE, sizeof(SharedData), IPC_CREAT | 0666); // shared mem space creation, setting needed permissions
    SharedData* data = (SharedData*)shmat(shmid, NULL, 0); // attach to shared mem
    
    int semid = semget(IPC_PRIVATE, 3, IPC_CREAT | 0666);
    semun arg;
    arg.val = 1;
    semctl(semid, 0, SETVAL, arg);
    semctl(semid, 1, SETVAL, arg);
    semctl(semid, 2, SETVAL, arg);

    load_rubric(data); // load rubric into shared mem
    load_exam(data, 1); // load first exam into shared mem
    data->finished = false; // data isnt finished when first loaded (must reach student 9999 for this to change)
    
    vector<pid_t> pids; // vector to keep track of all the pids
    for (int i = 0; i < num_tas; i++) { // allocating processes for all of the TA's
        pid_t pid = fork();
        if (pid == 0) {
            ta_process(i + 1, shmid, semid); // ta_process function call, increment TA id and pass in shared memory id
        } else {
            pids.push_back(pid); // add pid to vector space
        }
    }
    
    for (pid_t pid : pids) { 
        waitpid(pid, NULL, 0); // parent waiting for children processes
    }
    
    // shared memory cleanup
    shmdt(data); 
    shmctl(shmid, IPC_RMID, NULL);
    semctl(semid, 0, IPC_RMID, arg);
    
    cout << "all TAs finished marking" << endl;
    return 0;
}