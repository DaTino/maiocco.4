#ifndef P4QUEUE_H
#define P4QUEUE_H

//borrowing queue mess from geeksforgeeks...hope yalls don't mind
//https://www.geeksforgeeks.org/queue-set-1introduction-and-array-implementation/
typedef struct queueType {
  int front, rear, size;
  unsigned capacity;
  int* array
}queueType;

// function to create a queue of given capacity.
// It initializes size of queue as 0
queueType* createQueue(unsigned capacity) {
    int i;
    queueType* queue = (queueType*) malloc(sizeof(queueType));
    queue->capacity = capacity;
    queue->front = queue->size = 0;
    queue->rear = capacity - 1;
    queue->array = (int*) malloc(queue->capacity * sizeof(int)); // array of pids
    //start all spots on queue as -1 to indicate empty
    for(i = 0; i < capacity; i++){
        queue->array[i] = -1;
    }
    return queue;
}

// Queue is full when size becomes equal to the capacity
int isFull(queueType* queue) {
    return (queue->size == queue->capacity);
}

// Queue is empty when size is 0
int isEmpty(queueType* queue) {
  return (queue->size == 0);
}

// Function to add an item to the queue.
// It changes rear and size
void enqueue(queueType* queue, int item)
{
    if (isFull(queue)) {
        printf("QUEUE IS FULL\n");
        return;
    }
    queue->rear = (queue->rear + 1)%queue->capacity;
    queue->array[queue->rear] = item;
    queue->size = queue->size + 1;
    printf("%d enqueued to queue\n", item);
}

// Function to remove an item from queue.
// It changes front and size
int dequeue(queueType* queue) {
    if (isEmpty(queue)) {
        perror("oss.h: dequeue error: Cannot dequeue an empty queue.\n");
        return -1;
    }
    int item = queue->array[queue->front];
    queue->front = (queue->front + 1)%queue->capacity;
    queue->size = queue->size - 1;
    return item;
}

// Function to get front of queue
int front(queueType* queue) {
    if (isEmpty(queue)) {
        perror("oss.h: front error: Nothing is in front of queue\n");
        return -1;
    }
    return queue->array[queue->front];
}

// Function to get rear of queue
int rear(queueType* queue) {
    if (isEmpty(queue)) {
        perror("oss.h: rear error: Nothing is in back of queue\n");
        return -1;
    }
    return queue->array[queue->rear];
}

#endif
