// 
// Created by Melis Alibi (55850833) & Kabdolla Alnur (55819497)
//

#include <iostream>
#include <fstream>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <iomanip>
#include "generator.cpp"

using namespace std;

#define NUM_CRAWLERS 3
#define BUFFER_SIZE 12
#define NUM_CATEGORIES 13
#define FILENAME "textCorpus.txt"
#define LINEWIDTH 15

class Buffer {
private:
    char** articles;
    int capacity, size, front, rear;
public:
    Buffer() {
        articles = new char*[BUFFER_SIZE];
        capacity = BUFFER_SIZE;
        size = 0;
        front = 0;
        rear = -1;
    }
    bool full() { return size == capacity; }
    bool empty() { return size == 0; }
    void enqueue(char* article) {
    	if (full()) { return; }
        rear = (rear + 1) % capacity;
        articles[rear] = article;
        size++;
    }
    char* dequeue() {
    	if (empty()) { return NULL; }
        char* article = articles[front];
        front = (front + 1) % capacity;
        size--;
        return article;
    }
};

// Global variables as accessed by all threads
pthread_mutex_t mtx;
sem_t emptySpace, notEmpty;

Buffer buffer = Buffer();
int categories[NUM_CATEGORIES];
int articlesCount = 0;
bool quitSignal = false;
long interval_A, interval_B;

// Printing state messages in format based on indent
void display(char* message, int indent) {
    cout << setw(indent * LINEWIDTH) << message << "\n";
}
// Check if crawling goal is reached
bool enough() {
    for (int i = 0; i < 13; i++) {
        if (categories[i] < 5) {
            return false;
        }
    }
    return true;
}
// Creating file to write articles to
void writeToCorpus(int key, int label, char* article) {
    fstream file;

    file.open(FILENAME, ios_base::app);
    if (file.is_open()) {
        file << key << " " << label << " " << article << "\n";
        file.close();
    } else {
        ofstream outfile (FILENAME);
        outfile << key << " " << label << " " << article << "\n";
        outfile.close();
    }
}
// Classifying the articles
void classifyAndWrite(char* article) {
    int i = 0;
    bool upper = false;
    int classLabel;

    usleep(interval_B);

    while (!((article[i] >= 'A' && article[i] <= 'Z') || (article[i] >= 'a' && article[i] <= 'z'))) i++;
    if (article[i] >= 'A' && article[i] <= 'Z') upper = true;
    classLabel = int(article[i] - (upper ? 'A' : 'a')) % NUM_CATEGORIES + 1;

    articlesCount++;
    categories[classLabel - 1] += 1;
    writeToCorpus(articlesCount, classLabel, article);
}

void* crawler_routine(void *arg) {
    int id = *(int *)arg;
    bool waited = false; // variable to check if crawler sem_waited
    int emptySlots;
    display("start", id + 1);

    while (!quitSignal) { // do routine while classifier didn't signal quit
        sem_getvalue(&emptySpace, &emptySlots);
        if (emptySlots == 0) { // print wait if no space, waiting is handled by sem_wait
            display("wait", id + 1);
            waited = true;
        }
        sem_wait(&emptySpace); // wait if no empty space in buffer
        if (waited) {
            display("s-wait", id + 1);
            waited = false;
            if (quitSignal) break; // quit if while waiting classifier signaled quit
        }

        display("grab", id + 1);
        char* article = str_generator();
        usleep(interval_A);

        // Critical part of crawler
        pthread_mutex_lock(&mtx);
        buffer.enqueue(article);
        display("f-grab", id + 1);
        pthread_mutex_unlock(&mtx);

        sem_post(&notEmpty); // signal that buffer has article
    }
    display("quit", id + 1);
}

void* classifier_routine(void *arg) {
    bool done = false;
    display("start", 4);
    char* article;

    while (!done) {
        sem_wait(&notEmpty); // wait for buffer to have article before dequeue

        // Critical part of classifier
        pthread_mutex_lock(&mtx);
        display("clfy", 4);
        article = buffer.dequeue();
        pthread_mutex_unlock(&mtx);

        sem_post(&emptySpace); // signal that buffer cleared one article space

        classifyAndWrite(article); // Classifying and writing article to file
        display("f-clfy", 4);

        if (enough() && !quitSignal) { // Check if already enough, and didn't print before
            cout << setw(LINEWIDTH * 4 - 7) << articlesCount << "-enough" << "\n";
            quitSignal = true;
        }
        if (quitSignal && buffer.empty()) { // Continue classifying till buffer is empty
            done = true;
        }
    }
    cout << setw(LINEWIDTH * 4 - 6) << articlesCount << "-store" << "\n";
    display("quit", 4);
}

int main(int argc, char** argv) {
    pthread_t crawlers[NUM_CRAWLERS], classifier;
    int crawlerid[NUM_CRAWLERS], thread;

    interval_A = atoi(argv[1]);
    interval_B = atoi(argv[2]);

    // INITIALISING MUTEX & SEMAPHORES
    pthread_mutex_init(&mtx, 0);
    sem_init(&emptySpace, 0, BUFFER_SIZE);
    sem_init(&notEmpty, 0, 0);

    // PRINTING FIRST LINE
    for (int i = 0; i < NUM_CRAWLERS; i++) {
        cout << setw(LINEWIDTH - 1) << "crawler" << i + 1;
    }
    cout << setw(LINEWIDTH) << "classifier" << "\n";

    // CREATING CRAWLERS AND CLASSIFIER
    for (int i = 0; i < NUM_CRAWLERS; i++) {
        crawlerid[i] = i;
        thread = pthread_create(&crawlers[i], NULL, crawler_routine, (void *)&crawlerid[i]);
        if (thread) {
            cout << "Couldn't create crawler with id " << crawlerid[i] << "!\n";
            exit(EXIT_FAILURE);
        }
    }
    thread = pthread_create(&classifier, NULL, classifier_routine, NULL);
    if (thread) {
        cout << "Couldn't create classifier!\n";
        exit(EXIT_FAILURE);
    }

    // JOINING CRAWLERS AND CLASSIFIER
    for (int i = 0; i < NUM_CRAWLERS; i++) {
        thread = pthread_join(crawlers[i], NULL);
        if (thread) {
            cout << "Couldn't join crawler with id " << crawlerid[i] << "!\n";
            exit(EXIT_FAILURE);
        }
    }
    thread = pthread_join(classifier, NULL);
    if (thread) {
        cout << "Couldn't join classifier!\n";
        exit(EXIT_FAILURE);
    }

    // DESTROYING MUTEX & SEMAPHORES
    pthread_mutex_destroy(&mtx);
    sem_destroy(&emptySpace);
    sem_destroy(&notEmpty);

    pthread_exit(NULL);
}