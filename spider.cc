#include <cstdlib>
#include <iostream>
#include <string>
#include <queue>
#include <stdio.h>
#include <set>
#include "url.h"
#include "simplesocket.h"
using namespace std;

//Struct attaching current depth onto the url
struct urld {
  url_t addr;
  int depth;
};

//Struct consisting of the url, current depth, worker id it was processed by, and the buffer to parse
struct bufd {
  url_t addr;
  int worker_id;
  char buf [MAX_READ];
  int depth;
};

int MAX_DEPTH;
//This variable will represent how many threads are currently processing
//Both worker/parser, when they begin, add to it, when they are done, decrement
int inProgress = 0;
bool finished = false;

queue<urld> wq;
queue<bufd> pq;
set<string> dupeCheck;
bool * waiting;

//Lock for the worker queue
pthread_mutex_t workLock = PTHREAD_MUTEX_INITIALIZER;
//Lock for the parser queue
pthread_mutex_t parseLock = PTHREAD_MUTEX_INITIALIZER;
//Lock for my progess variable
pthread_mutex_t progLock = PTHREAD_MUTEX_INITIALIZER;
//Lock for outputting to the console
pthread_mutex_t coutLock = PTHREAD_MUTEX_INITIALIZER;

//Condition for ensuring the worker queue is not empty
pthread_cond_t workEmpty = PTHREAD_COND_INITIALIZER;
//Condition for ensuring the parser queue is not empty
pthread_cond_t parseEmpty = PTHREAD_COND_INITIALIZER;

//Condition to wait until the result has been parsed before worker continues
pthread_cond_t done = PTHREAD_COND_INITIALIZER;


void worker_thread(void * arg) {
  
  int thread_id = (int) arg;
  
  //If the worker queue is empty, and the parser queue is empty, and nothing is being process, we're done here
  while(!finished) {
    
	//Get access to worker queue
	pthread_mutex_lock(&workLock);
	
	//If still flagged to wait, go ahead and wait
	//If the thread has already been signaled, it will no longer be flagged
    while(waiting[thread_id]) {
	  pthread_cond_wait(&done, &workLock);
	  if(finished) {
	    //If everything is done, just let go of the lock and exit
		pthread_mutex_unlock(&workLock);
		break;
	  }
	}
	
	//If finished, break again, since the first break only took care of the first loop
	if(finished) break;
	
	//If the queue is empty, wait until the parser signals it just put something in it
	if(wq.empty() == true) {
        pthread_cond_wait(&workEmpty, &workLock);
		//If everything is done, just let go of the lock and exit
		if(finished) {
		  pthread_mutex_unlock(&workLock);
		  break;
		}
	}
	//Store the next url
	urld toCrawlD = wq.front();
	//Aquire access to and increment inProgress
	pthread_mutex_lock(&progLock);
	inProgress++;
	pthread_mutex_unlock(&progLock);
	//Remove the url from the queue and release the lock
	wq.pop();
	pthread_mutex_unlock(&workLock);
	
	//Do the crawling
	url_t toCrawl = toCrawlD.addr;
	int depth = toCrawlD.depth;
	
	bufd withDepth;
	withDepth.addr = toCrawl;
	withDepth.depth = depth;
	withDepth.worker_id = thread_id;
	
	clientsocket sock (toCrawl.host.c_str(), 80, 0, false);
	if (sock.connect()) {
		sprintf (withDepth.buf, "GET /%s HTTP/1.0\r\nHost: %s\r\n\r\n", toCrawl.file.c_str(),toCrawl.host.c_str());
		sock.write (withDepth.buf, strlen(withDepth.buf));
		
		int ret;
		int size = 0;
		sock.setTimeout(5);
		while ((ret = sock.read(withDepth.buf+size, MAX_READ-1-size)) > 0){
		  size += ret;
		}
	}
	sock.close();
	
	//Output action
	pthread_mutex_lock(&coutLock);
	cout << "requester " << thread_id << " url " << toCrawl.host << "/" << toCrawl.file << endl;
	pthread_mutex_unlock(&coutLock);
	
	//Remind itself to wait the next time through, unless parser finishes before and changes flag
	waiting[thread_id] = true;
	
	//Push onto parser queue
	pthread_mutex_lock(&parseLock);
	pq.push(withDepth);
	pthread_mutex_unlock(&parseLock);
	//Signal that something has been added to the parser queue
	pthread_cond_signal(&parseEmpty);

	//Show that it is no longer in progress
	pthread_mutex_lock(&progLock);
	inProgress--;
	pthread_mutex_unlock(&progLock);
  }
}

void parser_thread(void * arg) {

  //If the worker queue is empty, and the parser queue is empty, and nothing is being process, we're done here
  while(!finished) {
	
	//Get access to parser queue
	pthread_mutex_lock(&parseLock);
	
	//If the queue is empty, wait until a worker signals it just put something in it
    if(pq.empty() == true) {
      pthread_cond_wait(&parseEmpty, &parseLock);
	  //If everything is done, just let go of the lock and exit
	  if(finished) {
		pthread_mutex_unlock(&parseLock);
		break;
	  }
    }
    
	//Store the next buffer, show that it's in progress, and remove it from the queue
    bufd dBuf = pq.front();
    pthread_mutex_lock(&progLock);
	inProgress++;
	pthread_mutex_unlock(&progLock);
	pq.pop();
    pthread_mutex_unlock(&parseLock);
    
	//Parse it
    url_t toCrawl = dBuf.addr;
    int depth = dBuf.depth;
  
    set<url_t,lturl> urls;
    set<url_t,lturl>::iterator it;
  
    parse_URLs(dBuf.buf, MAX_READ, urls);
  
    //Output the action
    pthread_mutex_lock(&coutLock);
    cout << "service requester " << dBuf.worker_id << " url " << toCrawl.host << "/" << toCrawl.file << endl;
    pthread_mutex_unlock(&coutLock);
  
    //If the depth is not yet the maximum
    if(depth != MAX_DEPTH) {
	  //Increment depth for the resulting urls
	  depth++;
	  //For each result...
      for ( it=urls.begin() ; it != urls.end() ; it++ ) {
        urld toPush;
        toPush.addr = *it;
        toPush.depth = depth;
		
		string fullAddr = toPush.addr.host + "/" + toPush.addr.file;
	    
		//If the address has not already been crawled
		if(dupeCheck.count(fullAddr) == 0) {
		  //Add it to the list of already crawled urls
		  dupeCheck.insert(fullAddr);
		  //And push it onto the worker queue
		  pthread_mutex_lock(&workLock);
		  wq.push(toPush);
	      pthread_mutex_unlock(&workLock);
		  //Signal the workers something has just been put into the queue
	      pthread_cond_signal(&workEmpty);
		}
      }
    }
	
	//Flag the worker thread as no longer needing to wait
	//This is in case it arrives at the wait after it's signal
	waiting[dBuf.worker_id] = false;
	
	//Decrement inProgress
	pthread_mutex_lock(&progLock);
	inProgress--;
	pthread_mutex_unlock(&progLock);
	
	//Signal the worker if it's already waiting
	pthread_cond_signal(&done);
	
	pthread_mutex_lock(&workLock);
	pthread_mutex_lock(&progLock);
	pthread_mutex_lock(&parseLock);
	if(wq.empty() && pq.empty() && inProgress < 1) {
	  finished = true;
	}
	pthread_mutex_unlock(&workLock);
	pthread_mutex_unlock(&progLock);
	pthread_mutex_unlock(&parseLock);
  }
  //Once finished has been flagged, the loop will not re-iterate
  //Since everything is done, broadcast all workers who may be waiting on a condition
  pthread_cond_broadcast(&done);
  pthread_cond_broadcast(&workEmpty);
  pthread_cond_broadcast(&parseEmpty);
}

int main (int argc, char *argv[]) {

  string root = argv[1];
  MAX_DEPTH = atoi(argv[2]);
  int numWorkers = atoi(argv[3]);

  waiting = (bool*) malloc(numWorkers * sizeof(bool));
  for(int i = 0; i < numWorkers; i++) {
    waiting[i] = false;
  }
  
  //Parse the root and push it onto the worker queue with a depth of 0
  urld parsedRoot;
  parsedRoot.depth = 0;

  parse_single_URL(root, parsedRoot.addr.host, parsedRoot.addr.file);

  wq.push(parsedRoot);
  
  //Add the address to list of urls already crawled
  dupeCheck.insert(parsedRoot.addr.host + "/" +parsedRoot.addr.file);

  //Spawn all the threads
  int status;
  pthread_t thread_id[numWorkers+1];
  pthread_attr_t attr;
  
  status = pthread_attr_init(&attr);
  if (status) {
    cout << "pthread_attr_init returned " << status << endl;
    exit(1);
  }

  status = pthread_attr_setstacksize(&attr, 5*1024*1024);
  if (status) {
    cout << "pthread_attr_setstacksize returned " << status << endl;
    exit(1);
  }

  for(int i = 0; i < numWorkers; i++) {
    status = pthread_create(&thread_id[i], &attr, (void * (*)(void *)) worker_thread, (void *) i);
  }
  
  status = pthread_create(&thread_id[numWorkers], &attr, (void * (*)(void *)) parser_thread, (void *) numWorkers);
  
  //Join all threads
  pthread_join(thread_id[numWorkers], NULL);
  
  for(int i = 0; i < numWorkers; i++) {
    pthread_join(thread_id[i], NULL);
  }
  
  return 0;
}