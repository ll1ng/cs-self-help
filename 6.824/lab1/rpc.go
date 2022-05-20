package mr

//
// RPC definitions.
//
// remember to capitalize all names.
//

import (
	"os"
	"strconv"
)

//
// example to show how to declare the arguments
// and reply for an RPC.
//

const (
	MAP_TASK = iota
	REDUCE_TASK
	DONE    // finish all task, used in tasks from master
	SUCCESS //finish one task, used in tasks from worker
	FAIL
)

/* when a worker's free, send a message like this */
type Ask4TaskFromCoo struct {
}

/* coo send messages like below to hand out tasks */
type GetTaskFromCoo struct {
	TaskType int
	NReduce  int
	NMapTask int
	Valid    bool //whether receive valid info or not

	/* If map task */
	Filename  string
	MapNumber int

	/* If reduce task */
	Location int //mr-X-location
}

/* success message returned to master */
type WorkerReply struct {
	TaskType   int
	Success    bool
	TaskNumber int //M or R
	/* If map task */
	Location int

	/* If reduce task */
	/* If fail */
	ErrorMsg string
}

type ByKey []KeyValue

// Add your RPC definitions here.
// Example provided by caller, ExampleReply returned to caller
// if return is not nil,send a string to client	(erro occurs)
// and reply won't be return to client
// func (t *T) donnot_know(arg *WorkerReply, reply *Reply) error {
	// return nil
// }

// Cook up a unique-ish UNIX-domain socket name
// in /var/tmp, for the coordinator.
// Can't use the current directory since
// Athena AFS doesn't support UNIX-domain sockets.
func coordinatorSock() string {
	s := "/var/tmp/824-mr-"
	s += strconv.Itoa(os.Getuid())
	return s
}
