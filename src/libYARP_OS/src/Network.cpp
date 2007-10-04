// -*- mode:C++; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-

/*
 * Copyright (C) 2006 Paul Fitzpatrick
 * CopyPolicy: Released under the terms of the GNU GPL v2.0.
 *
 */

#include <ace/ACE.h>


#include <yarp/os/Network.h>
#include <yarp/os/Time.h>

#include <yarp/Companion.h>
#include <yarp/NameClient.h>
#include <yarp/NameConfig.h>
#include <yarp/Logger.h>
#include <yarp/String.h>
#include <yarp/os/Bottle.h>

#include <yarp/InputStream.h>
#include <yarp/OutputProtocol.h>
#include <yarp/Carriers.h>
#include <yarp/IOException.h>
#include <yarp/BufferedConnectionWriter.h>
#include <yarp/StreamConnectionReader.h>
#include <yarp/Route.h>
#include <yarp/PortCommand.h>

using namespace yarp;
using namespace yarp::os;

bool Network::connect(const char *src, const char *dest, 
                      const char *carrier, bool quiet) {
    int result = -1;
    if (carrier!=NULL) {
        // prepend carrier
        String fullDest = String(carrier) + ":/" + Companion::slashify(dest);
        result = Companion::connect(src,fullDest.c_str(),quiet);
    } else {
        result = Companion::connect(src,dest,quiet);
    }
    return result == 0;

}


bool Network::disconnect(const char *src, const char *dest, bool quiet) {
    int result = Companion::disconnect(src,dest,quiet);
    return result == 0;
}

bool Network::sync(const char *port, bool quiet) {
    //if (!result) {
    int result = Companion::wait(port,quiet);
    //}
    if (result==0) {
        Companion::poll(port,true);
    }
    return result == 0;
}


int Network::main(int argc, char *argv[]) {
    return Companion::main(argc,argv);
}


void Network::init() {

    // Broken pipes need to be dealt with through other means
    ACE_OS::signal(SIGPIPE, SIG_IGN);

    ACE::init();
    String verbose = NameConfig::getEnv("YARP_VERBOSE");
    Bottle b(verbose.c_str());
    if (b.get(0).asInt()>0) {
        YARP_INFO(Logger::get(), "YARP_VERBOSE environment variable is set");
        Logger::get().setVerbosity(b.get(0).asInt());
    }
    Logger::get().setPid();
	// make sure system is actually able to do things fast
	Time::turboBoost();
}

void Network::fini() {
    ACE::fini();
}

Contact Network::queryName(const char *name) {
    NameClient& nic = NameClient::getNameClient();
    Address address = nic.queryName(name);
    return address.toContact();
}


Contact Network::registerName(const char *name) {
    NameClient& nic = NameClient::getNameClient();
    Address address = nic.registerName(name);
    return address.toContact();
}


Contact Network::registerContact(const Contact& contact) {
    NameClient& nic = NameClient::getNameClient();
    Address address = nic.registerName(contact.getName().c_str(),
                                       Address::fromContact(contact));
    return address.toContact();
}

Contact Network::unregisterName(const char *name) {
    NameClient& nic = NameClient::getNameClient();
    Address address = nic.unregisterName(name);
    return address.toContact();
}


Contact Network::unregisterContact(const Contact& contact) {
    NameClient& nic = NameClient::getNameClient();
    Address address = nic.unregisterName(contact.getName().c_str());
    return address.toContact();
}

bool Network::setLocalMode(bool flag) {
    NameClient& nic = NameClient::getNameClient();
    bool state = nic.isFakeMode();
    nic.setFakeMode(flag);
    return state;
}


void Network::assertion(bool shouldBeTrue) {
    // could replace with ACE assertions, except should not 
    // evaporate in release mode
    YARP_ASSERT(shouldBeTrue);
}


ConstString Network::readString(bool *eof) {
    return ConstString(Companion::readString(eof).c_str());
}



bool Network::write(const Contact& contact, 
                    PortWriter& cmd,
                    PortReader& reply) {

    // This is a little complicated because we make the connection
    // without using a port ourselves.  With a port it is easy.

    try {
        const char *connectionName = "anon";
        ConstString name = contact.getName();
        const char *targetName = name.c_str();  // use carefully!
        Address address = Address::fromContact(contact);
        if (!address.isValid()) {
            NameClient& nic = NameClient::getNameClient();
            address = nic.queryName(targetName);
        }
        if (!address.isValid()) {
            YARP_ERROR(Logger::get(),"could not find port");
            return false;
        }

        OutputProtocol *out = Carriers::connect(address);
        if (out==NULL) {
            YARP_ERROR(Logger::get(),"cannot connect to port");
            return false;
        }
        //printf("RPC connection to %s at %s (connection name %s)\n", targetName, 
        //     address.toString().c_str(),
        //     connectionName);
        Route r(connectionName,targetName,"text_ack");
        out->open(r);
        OutputStream& os = out->getOutputStream();
        InputStream& is = out->getInputStream();
        StreamConnectionReader reader;

        PortCommand pc(0,"d");
        BufferedConnectionWriter bw(out->isTextMode());
        bool ok = pc.write(bw);
        if (!ok) {
            throw IOException("writer failed");
        }
        ok = cmd.write(bw);
        if (!ok) {
            throw IOException("writer failed");
        }
        bw.write(os);
        Bottle resp;
        reader.reset(is,NULL,r,0,out->isTextMode());
        reply.read(reader);
        if (out!=NULL) {
            delete out;
            out = NULL;
        }
        return true;
    } catch (IOException e) {
        YARP_ERROR(Logger::get(),"write failed");
        // should deallocate stream if allocated
        return false;
    }
    return true;
}


