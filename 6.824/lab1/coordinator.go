package mr

import (
	//	"fmt"
	"log"
	"net"
	"net/http"
	"net/rpc"
	"os"
	"sync"
)

type Coordinator struct {
	// Your definitions here.
	// finish map[string]bool
	filename        []string
	mapFinish       []bool
	reduceFinish    []bool
	mapHandedOut    []bool
	reduceHandedout []bool
	nMapTask        int
	nreduceTask     int
	isDone          bool
	isMapDone       bool
	mapDuration     []int //[0,10] plus
	reduceDuration  []int
	/* concurrency*/
	mu sync.Mutex
}

type Args Ask4TaskFromCoo
type Reply GetTaskFromCoo

// Your code here -- RPC handlers for the worker to call.

//
// an example RPC handler.
//
// the RPC argument and reply types are defined in rpc.go.
//
func (c *Coordinator) HandoutTask(args *Args, reply *Reply) error {

	c.mu.Lock()
	defer c.mu.Unlock()
	c.DurationPlus1()
	if c.isDone == true {
		ConstructDoneTask(c, reply)
		return nil
	}

	if c.isMapDone == false {
		for idx, filename := range c.filename {
			if c.mapFinish[idx] == false {
				if c.mapHandedOut[idx] == false {
					ConstructMapTask(c, reply, idx, filename)
					c.mapHandedOut[idx] = true
					return nil
				} else {
					if c.mapDuration[idx] == 10 {
						ConstructMapTask(c, reply, idx, filename)
						c.mapHandedOut[idx] = true
						return nil
					}
				}
			}
		}
	} else {
		for i := 0; i < c.nreduceTask; i++ {
			if c.reduceFinish[i] == false {
				if c.reduceHandedout[i] == false {
					ConstructReduceTask(c, reply, i)
					c.reduceHandedout[i] = true
					return nil
				} else {
					if c.reduceDuration[i] == 10 {
						ConstructReduceTask(c, reply, i)
						c.reduceHandedout[i] = true
						return nil
					}
				}
			}
		}
	}
	return nil
}

func (c *Coordinator) UpdateState(args *WorkerReply, reply *Reply) error {
	/* If fail, ignore*/
	c.mu.Lock()
	defer c.mu.Unlock()
	if args.Success {
		switch args.TaskType {
		case MAP_TASK:
			c.mapFinish[args.TaskNumber] = true
		case REDUCE_TASK:
			c.reduceFinish[args.TaskNumber] = true
		}
	} else {
		return nil
	}

	terminate := -1
	if c.isMapDone == false {
		for idx, mapped := range c.mapFinish {
			if mapped == false {
				terminate = idx
				break
			}
		}
		if terminate == -1 {
			c.isMapDone = true
			return nil
		}
	} else {
		for idx, mapped := range c.reduceFinish {
			if mapped == false {
				terminate = idx
				break
			}
		}
		if terminate == -1 {
			c.isDone = true
		}
	}
	return nil
}

//
// start a thread that listens for RPCs from worker.go
//
func (c *Coordinator) server() {
	rpc.Register(c)
	rpc.HandleHTTP()
	//l, e := net.Listen("tcp", ":1234")
	sockname := coordinatorSock()
	os.Remove(sockname)
	l, e := net.Listen("unix", sockname)
	if e != nil {
		log.Fatal("listen error:", e)
	}
	go http.Serve(l, nil)
}

//
// main/mrcoordinator.go calls Done() periodically to find out
// if the entire job has finished.
//
func (c *Coordinator) Done() bool {
	// ret := false

	// Your code here.
	c.mu.Lock()
	defer c.mu.Unlock()
	return c.isDone
}

//
// create a Coordinator.
// main/mrcoordinator.go calls this function.
// nReduce is the number of reduce tasks to use.
//
func MakeCoordinator(files []string, nReduce int) *Coordinator {
	c := Coordinator{}

	// Your code here.
	c.filename = os.Args[1:]
	c.nMapTask = len(os.Args[1:])
	c.nreduceTask = nReduce
	c.isDone = false
	c.isMapDone = false
	c.mapFinish = make([]bool, len(os.Args[1:]))
	c.reduceFinish = make([]bool, c.nreduceTask)
	c.mapHandedOut = make([]bool, len(os.Args[1:]))
	c.reduceHandedout = make([]bool, c.nreduceTask)
	c.mapDuration = make([]int, len(os.Args[1:]))
	c.reduceDuration = make([]int, c.nreduceTask)

	c.server()
	return &c
}

func ConstructMapTask(c *Coordinator, reply *Reply, mapidx int, filename string) {
	reply.Filename = filename
	reply.TaskType = MAP_TASK
	reply.MapNumber = mapidx
	reply.NMapTask = c.nMapTask
	reply.NReduce = c.nreduceTask
	reply.Valid = true
}

func ConstructReduceTask(c *Coordinator, reply *Reply, reduceNumber int) {
	reply.TaskType = REDUCE_TASK
	reply.Location = reduceNumber
	reply.NReduce = c.nreduceTask
	reply.NMapTask = c.nMapTask
	reply.Valid = true
}

func ConstructDoneTask(c *Coordinator, reply *Reply) {
	reply.TaskType = DONE
	reply.Valid = true
}

func (c *Coordinator) DurationPlus1() {
	for i := 0; i < c.nMapTask; i++ {
		if c.mapHandedOut[i] {
			if c.mapDuration[i] < 10 {
				c.mapDuration[i]++
			} else {
				c.mapHandedOut[i] = false
				c.mapDuration[i] = 0
			}
		}
	}
	for i := 0; i < c.nreduceTask; i++ {
		if c.reduceHandedout[i] {
			if c.reduceDuration[i] < 10 {
				c.reduceDuration[i]++
			} else {
				c.reduceHandedout[i] = false
				c.reduceDuration[i] = 0
			}
		}
	}
}
