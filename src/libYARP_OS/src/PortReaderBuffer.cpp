// -*- mode:C++; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-

#include <yarp/os/PortReaderBuffer.h>
#include <yarp/os/Thread.h>

#include <yarp/Logger.h>
#include <yarp/SemaphoreImpl.h>

#include <ace/Vector_T.h>
#include <ace/Containers_T.h>

using namespace yarp;
using namespace yarp::os;



class PortReaderPacket {
public:
    PortReaderPacket *prev_, *next_;
    PortReader *reader;

    PortReaderPacket() {
        prev_ = next_ = NULL;
        reader = NULL;
        reset();
    }

    virtual ~PortReaderPacket() {
        reset();
    }

    void reset() {
        if (reader!=NULL) {
            delete reader;
            reader = NULL;
        }
    }

    PortReader *getReader() {
        return reader;
    }

    void setReader(PortReader *reader) {
        reset();
        this->reader = reader;
    }
};


class PortReaderPool {
private:
    ACE_Double_Linked_List<PortReaderPacket> inactive, active;

public:

    int getCount() {
        return active.size();
    }

    int getFree() {
        return inactive.size();
    }

    PortReaderPacket *getInactivePacket() {
        if (inactive.is_empty()) {
            size_t obj_size = sizeof (PortReaderPacket);
            PortReaderPacket *obj = NULL;
            ACE_NEW_MALLOC_RETURN (obj,
                                   (PortReaderPacket *)
                                   ACE_Allocator::instance()->malloc(obj_size),
                                   PortReaderPacket(), 0);
            inactive.insert_tail(obj);
        }
        PortReaderPacket *next = NULL;
        inactive.get(next);
        YARP_ASSERT(next!=NULL);
        inactive.remove(next);
        //active.insert_tail(next);
        return next;
    }

    PortReaderPacket *getActivePacket() {
        PortReaderPacket *next = NULL;
        if (getCount()>=1) {
            active.get(next);
            YARP_ASSERT(next!=NULL);
            active.remove(next);
        }
        return next;
    }

    void addActivePacket(PortReaderPacket *packet) {
        if (packet!=NULL) {
            active.insert_tail(packet);
        }
    }

    void addInactivePacket(PortReaderPacket *packet) {
        if (packet!=NULL) {
            inactive.insert_tail(packet);
        }
    }

    void reset() {
        active.reset();
        inactive.reset();
    }
};



class PortReaderBufferBaseHelper {
private:

    PortReaderBufferBase& owner;
    PortReaderPacket *prev;

public:

    PortReaderPool pool;

    int ct;
	Port *port;
    SemaphoreImpl contentSema;
    SemaphoreImpl consumeSema;
    SemaphoreImpl stateSema;

    PortReaderBufferBaseHelper(PortReaderBufferBase& owner) : 
        owner(owner), contentSema(0), consumeSema(0), stateSema(1) {
        prev = NULL;
		port = NULL;
        ct = 0;
    }

    virtual ~PortReaderBufferBaseHelper() {
        Port *closePort = NULL;
        stateSema.wait();
		if (port!=NULL) {
			closePort = port;
		}
        stateSema.post();
        if (closePort!=NULL) {
            closePort->close();
        }
        stateSema.wait();
        clear();
        //stateSema.post();  // never give back mutex
    }

    void clear() {
        if (prev!=NULL) {
            pool.addInactivePacket(prev);
            prev = NULL;
        }
        pool.reset();
        ct = 0;
    }


    PortReaderPacket *get() {
        PortReaderPacket *result = NULL;
        bool grab = true;
        if (pool.getFree()==0) {
            grab = false;
            unsigned int maxBuf = owner.getMaxBuffer();
            if (maxBuf==0 || (pool.getFree()+pool.getCount())<maxBuf) {
                grab = true;
            } else {
                // ok, can't get free, clean space.
                // here would be a good place to do buffer reuse.
            }
        }
        if (grab) {
            result = pool.getInactivePacket();
            if (result->getReader()==NULL) {
                PortReader *next = owner.create();
                YARP_ASSERT(next!=NULL);
                result->setReader(next);
            }
        }

        return result;
    }

    bool checkContent() {
        return pool.getCount()>0;
    }

    PortReaderPacket *getContent() {
        if (prev!=NULL) {
            pool.addInactivePacket(prev);
            prev = NULL;
        }
        if (pool.getCount()>=1) {
            prev = pool.getActivePacket();
            ct--;
        }
        return prev;
    }

	void attach(Port& port) {
		this->port = &port;
		port.setReader(owner);
	}
};


// implementation is a list
#define HELPER(x) (*((PortReaderBufferBaseHelper*)(x)))

void PortReaderBufferBase::init() {
    implementation = new PortReaderBufferBaseHelper(*this);
    YARP_ASSERT(implementation!=NULL);
}

PortReaderBufferBase::~PortReaderBufferBase() {
    if (implementation!=NULL) {
        delete &HELPER(implementation);
        implementation = NULL;
    }
}

void PortReaderBufferBase::release(PortReader *completed) {
    //HELPER(implementation).stateSema.wait();
    //HELPER(implementation).configure(completed,true,false);
    //HELPER(implementation).stateSema.post();
    printf("release not implemented anymore; not needed\n");
    exit(1);
}

bool PortReaderBufferBase::check() {
    HELPER(implementation).stateSema.wait();
    bool ok = HELPER(implementation).checkContent();
    HELPER(implementation).stateSema.post();
    return ok;
}

PortReader *PortReaderBufferBase::readBase() {
    HELPER(implementation).contentSema.wait();
    HELPER(implementation).stateSema.wait();
    PortReaderPacket *readerPacket = HELPER(implementation).getContent();
    PortReader *reader = NULL;
    if (readerPacket!=NULL) reader = readerPacket->getReader();
    HELPER(implementation).stateSema.post();
    if (reader!=NULL) {
        HELPER(implementation).consumeSema.post();
    }
    return reader;
}


bool PortReaderBufferBase::read(ConnectionReader& connection) {
    PortReaderPacket *reader = NULL;
    while (reader==NULL) {
        HELPER(implementation).stateSema.wait();
        reader = HELPER(implementation).get();
        HELPER(implementation).stateSema.post();
        if (reader==NULL) {
            HELPER(implementation).consumeSema.wait();
        }
    }
    bool ok = false;
    if (connection.isValid()) {
        YARP_ASSERT(reader->getReader()!=NULL);
        ok = reader->getReader()->read(connection);
	} else {
		// this is a disconnection
		// don't talk to this port ever again
		HELPER(implementation).port = NULL;
	}
    if (ok) {
        HELPER(implementation).stateSema.wait();
        bool pruned = false;
        if (HELPER(implementation).ct>0&&prune) {
            PortReaderPacket *readerPacket = 
                HELPER(implementation).getContent();
            PortReader *reader = NULL;
            if (readerPacket!=NULL) reader = readerPacket->getReader();
            pruned = (reader!=NULL);
        }
        //HELPER(implementation).configure(reader,false,true);
        HELPER(implementation).pool.addActivePacket(reader);
        HELPER(implementation).ct++;
        HELPER(implementation).stateSema.post();
        if (!pruned) {
            HELPER(implementation).contentSema.post();
        }
        //YARP_ERROR(Logger::get(),">>>>>>>>>>>>>>>>> adding data");
    } else {
        HELPER(implementation).stateSema.wait();
        HELPER(implementation).pool.addInactivePacket(reader);
        HELPER(implementation).stateSema.post();
        //YARP_ERROR(Logger::get(),">>>>>>>>>>>>>>>>> skipping data");

        // important to give reader a shot anyway, allowing proper closing
        YARP_DEBUG(Logger::get(), "giving PortReaderBuffer chance to close");
        HELPER(implementation).contentSema.post();
    }
    return ok;
}


void PortReaderBufferBase::setAutoRelease(bool flag) {
    //HELPER(implementation).stateSema.wait();
    //HELPER(implementation).setAutoRelease(flag);
    //HELPER(implementation).stateSema.post();
    printf("setAutoRelease not implemented anymore; not needed\n");
    exit(1);
}

void PortReaderBufferBase::attachBase(yarp::os::Port& port) {
    HELPER(implementation).attach(port);
}

bool PortReaderBufferBase::isClosed() {
	return HELPER(implementation).port==NULL;
}




