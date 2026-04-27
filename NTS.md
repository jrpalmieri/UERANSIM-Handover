Network Task System (NTS)
/utils/nts.hpp

Each task in the system is an NTS class that is started as a separate thread.  Each task manages a message queue where other tasks can add NTS messages for the task to process, and is protected by a mutex and condition variable.  Each task also has a private priority queue for its timers, sorted by end times.

A take() method is used to operate on the queues.  It locks the mutex and checks the message queue for a waiting message at the front of the queue.  If one exists, it removes and processes it.  If the queue is empty, it waits on a condition variable for the first of a timer expiration or 500ms.  After the wait, it locks the mutex and checks the message queue again, and if a message available, it processes it.  If no messages are available, the priority queue is checked for a timer expiration, and if found, it is processed.

Note that the take() process is structured so that a timer expiration will not be serviced until a message queue is found to be empty on both checks (but the expiration will cause the condition variable to be immediately true).