#include <iostream>
#include <sys/shm.h> // shared memory import
#include <sys/wait.h> // process syncing
#include <unistd.h> // unix standard lib
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <vector>

using namespace std;

struct SharedData {
    char rubric[5][10];
    char current_exam[5];
    int questions_marked[5];
    int current_student_num;
    bool finished;
};

double random_delay(double min, double max) { // random delay function, used for both reviewing/correcting rubric delay and marking delay
    return min + (rand() / (double)RAND_MAX) * (max - min); // 0.5 + some random number mulitpled by (max - min)
}

void load_rubric(SharedData* data) { // reads rubric.txt file and loads into shared memory
    ifstream file("rubric.txt");
    if (!file.is_open()) { // if file couldn't be open
        cerr << "error: could not open rubric.txt" << endl;
        exit(1);
    }
    
    for (int i = 0; i < 5; i++) {
        file.getline(data->rubric[i], 10); // parse through every line
    }
    
    file.close(); // close the file
}

void load_exam(SharedData* data, int student_num) { // pass in shared memory and student number to load in their exams
    data->current_student_num = student_num;
    for (int i = 0; i < 5; i++) {
        data->questions_marked[i] = 0; // parse through the lines of the exam
    }
    cout << "loaded exam for student " << student_num << endl;
}

int get_next_exam(int current) { // get next exam in shared memory, starting from the current exam being viewed
    ifstream file("exam_" + to_string(current + 1).insert(0, 4 - to_string(current + 1).length(), '0') + ".txt"); // pass in next exam file name
    if (file.is_open()) { // open file
        int student_num;
        file >> student_num; // extract student number for new exam file
        file.close(); // close file
        return student_num; // return student number
    }
    return -1; // if failed to get next exam, return -1
}

void review_rubric(int ta_id, SharedData* data) { // TA reviews the rubric
    cout << "TA " << ta_id << ": reviewing rubric" << endl;
    
    for (int i = 0; i < 5; i++) { // for every exercise in the rubric
        double delay = random_delay(0.5, 1.0); // get the delay
        usleep(delay * 1000000); // function wide sleep
        
        if (rand() % 2 == 0) { // if a random number happens to fit this condition, since no explict correcting logic was mentioned in the assignment pdf
            cout << "TA " << ta_id << ": correcting rubric question " << (i + 1) << endl;
            char current = data->rubric[i][2];
            data->rubric[i][2] = current + 1;
            cout << "TA " << ta_id << ": rubric question " << (i + 1) << " changed from '" << current << "' to '" << data->rubric[i][2] << "'" << endl;
        }
    }
}

void mark_questions(int ta_id, SharedData* data) { // TA marking a exercise
    while (!data->finished) { // while we still have students to mark
        int question = -1; 
        
        for (int i = 0; i < 5; i++) { // parse through all of the exercises
            if (data->questions_marked[i] == 0) { // if a question isnt marked
                question = i;
                data->questions_marked[i] = 1; // if a question is marked
                break;
            }
        }
        
        if (question == -1) { // if all questions were marked for the current exam
            int next_student = get_next_exam(data->current_student_num);
            if (next_student == -1) { // if no more exams, terminate
                data->finished = true;
                return;
            }
            load_exam(data, next_student); // load next exam
            if (next_student == 9999) { // if student 9999
                data->finished = true; // mark it normally then terminate after
            }
            continue;
        }
        
        cout << "TA " << ta_id << ": marking exam " << data->current_student_num << " question " << (question + 1) << endl;
        double delay = random_delay(1.0, 2.0); // marking delay
        usleep(delay * 1000000); // function wide sleep
        cout << "TA " << ta_id << ": finished marking exam " << data->current_student_num << " question " << (question + 1) << endl;
    }
}

void ta_process(int ta_id, int shmid) { // function to create TA process
    srand(time(NULL) + ta_id); // random seed for TA
    
    SharedData* data = (SharedData*)shmat(shmid, NULL, 0); // attach the TA process to shared mem 
    
    review_rubric(ta_id, data); // review rubrics
    mark_questions(ta_id, data); // mark questions
    
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
    
    load_rubric(data); // load rubric into shared mem
    load_exam(data, 1); // load first exam into shared mem
    data->finished = false; // data isnt finished when first loaded (must reach student 9999 for this to change)
    
    vector<pid_t> pids; // vector to keep track of all the pids
    for (int i = 0; i < num_tas; i++) { // allocating processes for all of the TA's
        pid_t pid = fork();
        if (pid == 0) {
            ta_process(i + 1, shmid); // ta_process function call, increment TA id and pass in shared memory id
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
    
    cout << "all TAs finished marking" << endl;
    return 0;
}