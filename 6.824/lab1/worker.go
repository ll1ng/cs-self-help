package mr

import (
	"encoding/json"
	"fmt"
	"hash/fnv"
	"io/ioutil"
	"log"
	"net/rpc"
	"os"
	"sort"
	"time"
)

//
// Map functions return a slice of KeyValue.
//
type KeyValue struct {
	Key   string
	Value string
}

// for sorting by key.
func (a ByKey) Len() int           { return len(a) }
func (a ByKey) Swap(i, j int)      { a[i], a[j] = a[j], a[i] }
func (a ByKey) Less(i, j int) bool { return a[i].Key < a[j].Key }

//
// use ihash(key) % NReduce to choose the reduce
// task number for each KeyValue emitted by Map.
//
func ihash(key string) int {
	h := fnv.New32a()
	h.Write([]byte(key))
	return int(h.Sum32() & 0x7fffffff)
}

func finalizeReduceFile(tmpFile string, taskN int) {
	finalFile := fmt.Sprintf("mr-out-%d", taskN)
	os.Rename(tmpFile, finalFile)
}

func getIntermediateFile(mapTaskN int, reduceTaskN int) string {
	return fmt.Sprintf("mr-%d-%d", mapTaskN, reduceTaskN)
}

func finalizeIntermediateFile(tmpFile string, mapTaskN int, reduceTaskN int) {
	finalFile := getIntermediateFile(mapTaskN, reduceTaskN)
	os.Rename(tmpFile, finalFile)
}

//
// main/mr-tmp/mrworker.go calls this function.
//
func Worker(mapf func(string, string) []KeyValue,
	reducef func(string, []string) string) {

	// Your worker implementation here.
	for {
		args := Ask4TaskFromCoo{}
		reply := GetTaskFromCoo{}
		call("Coordinator.HandoutTask", &args, &reply)

		if reply.Valid == true {
			switch reply.TaskType {
			case MAP_TASK:
				HandleMap(&reply, mapf)
			case REDUCE_TASK:
				HandleReduce(&reply, reducef)
			case DONE:
				os.Exit(0)
			}
		}
		time.Sleep(time.Second)
	}
}

func HandleMap(reply *GetTaskFromCoo,
	mapf func(string, string) []KeyValue) {

	intermediate := []KeyValue{}
	R := reply.NReduce
	mapidx := reply.MapNumber
	file, err := os.Open(reply.Filename)
	if err != nil {
		log.Fatalf("cannot open %v", reply.Filename)
	}
	content, err := ioutil.ReadAll(file)
	if err != nil {
		log.Fatalf("cannot read %v", reply.Filename)
	}
	file.Close()
	kva := mapf(reply.Filename, string(content))
	intermediate = append(intermediate, kva...)

	tmpFiles := [10]*os.File{}
	tmpFilenames := []string{}
	i := 0
	idx := 0

	for idx = 0; idx < R; idx++ {
		tmpFiles[idx], err = ioutil.TempFile("", "")
		if err != nil {
			log.Fatal("cannot open tmpfile")
		}
		fmt.Fprintf(tmpFiles[idx], "[")
		tmpFilenames = append(tmpFilenames, tmpFiles[idx].Name())
	}

	length := len(intermediate)
	for ; i < length; i++ {
		data, _ := json.Marshal(intermediate[i])
		fmt.Fprintf(tmpFiles[ihash(intermediate[i].Key)%R], "%v,", string(data))
	}

	idx = 0
	for idx = 0; idx < R; idx++ {
		tmpFiles[idx].Seek(-1, 1)
		fmt.Fprint(tmpFiles[idx], "]")
		tmpFiles[idx].Close()
		finalizeIntermediateFile(tmpFilenames[idx], mapidx, idx)
	}
	if llog {
		fmt.Println("Close finish")
	}

	args := WorkerReply{}
	args.TaskType = MAP_TASK
	args.TaskNumber = mapidx
	args.Success = true
	call("Coordinator.UpdateState", &args, &reply)
}

func HandleReduce(reply *GetTaskFromCoo,
	reducef func(string, []string) string) {
	location := reply.Location
	maplen := reply.NMapTask
	ofile := [10]*os.File{}
	intermediate := []KeyValue{}

	var err error
	for idx := 0; idx < maplen; idx++ {
		kva := []KeyValue{}
		ofile[idx], err = os.Open(fmt.Sprintf("mr-%v-%v", idx, location))

		if err != nil {
			log.Fatalf("cannot open mr-%v-%v", idx, location)
		}
		data, err := ioutil.ReadAll(ofile[idx])

		if err != nil {
			log.Fatalf("cannot read mr-%v-%v", idx, location)
		}

		err = json.Unmarshal(data, &kva)
		if err != nil {
			ofile[idx].Close()
			continue
		}

		intermediate = append(intermediate, kva...)
		ofile[idx].Close()
	}
	sort.Sort(ByKey(intermediate))
	oname := fmt.Sprintf("mr-out-%v", location)
	oreducefile, err := os.Create(oname)
	if err != nil {
		log.Fatalf("cannot open mr-out-%v", location)
	}

	i := 0

	for i < len(intermediate) {
		j := i + 1
		for j < len(intermediate) && intermediate[j].Key == intermediate[i].Key {
			j++
		}
		values := []string{} //[1,1,1,]
		for k := i; k < j; k++ {
			values = append(values, intermediate[k].Value)
		}
		output := reducef(intermediate[i].Key, values)

		// this is the correct format for each line of Reduce output.
		// intermediate_merged=append(intermediate_merged, KeyValue{(intermediate[i].Key:output)})
		fmt.Fprintf(oreducefile, "%v %v\n", intermediate[i].Key, output)

		i = j
	}
	oreducefile.Close()

	args := WorkerReply{}
	args.TaskType = REDUCE_TASK
	args.TaskNumber = location
	args.Success = true
	call("Coordinator.UpdateState", &args, &reply)
}

//
// send an RPC request to the coordinator, wait for the response.
// usually returns true.
// returns false if something goes wrong.
//
func call(rpcname string, args interface{}, reply interface{}) bool {
	// c, err := rpc.DialHTTP("tcp", "127.0.0.1"+":1234")
	sockname := coordinatorSock()
	c, err := rpc.DialHTTP("unix", sockname)
	if err != nil {
		log.Fatal("dialing:", err)
	}
	defer c.Close()

	err = c.Call(rpcname, args, reply)
	if err == nil {
		return true
	}

	fmt.Println(err)
	return false
}
