# ipc_project
CS 300 IPC project. Performs matrix multiplication by using two separate processes with multiple threads for each.

There are two programs, "package" and "compute", that work together to multiply two matrices. Descriptions of each program:
PACKAGE:

 * Package reads in two input matrices and creates threads that divide the
 * matrix into subtasks to be sent to the message queue for completion. After
 * each thread sends its message, it waits for a message containing completed
 * results which are used to build the resulting matrix.

Usage: ./package <matrix1.file> <matrix2.file> <output.file> <thread_delay>

COMPUTE:
 * Compute generates a pool of threads specified via command line argument
 * which receive jobs from the message queue. Upon completing each calculation,
 * the results are sent back to the queue. This program runs indefinitely and
 * must be terminated with Ctrl-\
 
Usage: ./compute <num_threads> <optional: sendMessages>
The last command line argument is optional, and entering -n as that argument will
cause compute to not send any messages.

NOTE: These programs were produced as a class project, and as such assume proper usage.
      Incorrect command line argument formats will most likely produce unintended results.
