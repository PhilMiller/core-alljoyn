/**
 * @file
 * @brief  Sample implementation of an AllJoyn client. That has an implmentation
 * of a secure client that uses the ECDHE key exchange.
 */

/******************************************************************************
 *
 *
 * Copyright AllSeen Alliance. All rights reserved.
 *
 *    Permission to use, copy, modify, and/or distribute this software for any
 *    purpose with or without fee is hereby granted, provided that the above
 *    copyright notice and this permission notice appear in all copies.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 ******************************************************************************/
#include <qcc/platform_cpp.h>

#include <assert.h>
#include <signal.h>
#include <stdio.h>

#include <qcc/Log.h>
#include <qcc/String.h>

#include <alljoyn/AllJoynStd.h>
#include <alljoyn/BusAttachment.h>
#include <alljoyn/Init.h>
#include <alljoyn/Status.h>
#include <alljoyn/version.h>
#include <qcc/time.h>
#include <qcc/CryptoECC.h>
#include <qcc/CertificateECC.h>
#include <qcc/Log.h>

using namespace std;
using namespace qcc;
using namespace ajn;

/** Static top level message bus object */
static BusAttachment* g_msgBus = NULL;

/*constants*/
static const char* INTERFACE_NAME = "org.alljoyn.bus.samples.secure.SecureInterface";
static const char* SERVICE_NAME = "org.alljoyn.bus.samples.secure";
static const char* SERVICE_PATH = "/SecureService";
static const char* KEYX_ECDHE_NULL = "ALLJOYN_ECDHE_NULL";
static const char* KEYX_ECDHE_PSK = "ALLJOYN_ECDHE_PSK";
static const char* KEYX_ECDHE_ECDSA = "ALLJOYN_ECDHE_ECDSA";
static const char* ECDHE_KEYX = "ALLJOYN_ECDHE_ECDSA ALLJOYN_ECDHE_PSK ALLJOYN_ECDHE_NULL";
static const SessionPort SERVICE_PORT = 42;

static bool s_joinComplete = false;
static String s_sessionHost;
static SessionId s_sessionId = 0;

static volatile sig_atomic_t s_interrupt = false;

static void CDECL_CALL SigIntHandler(int sig)
{
    s_interrupt = true;
}


/** AllJoynListener receives discovery events from AllJoyn */
class MyBusListener : public BusListener, public SessionListener {
  public:
    void FoundAdvertisedName(const char* name, TransportMask transport, const char* namePrefix)
    {
        printf("FoundAdvertisedName(name='%s', transport = 0x%x, prefix='%s')\n", name, transport, namePrefix);
        if (0 == strcmp(name, SERVICE_NAME) && s_sessionHost.empty()) {
            /* We found a remote bus that is advertising basic service's  well-known name so connect to it */
            /* Since we are in a callback we must enable concurrent callbacks before calling a synchronous method. */
            s_sessionHost = name;
            g_msgBus->EnableConcurrentCallbacks();
            SessionOpts opts(SessionOpts::TRAFFIC_MESSAGES, false, SessionOpts::PROXIMITY_ANY, TRANSPORT_ANY);
            QStatus status = g_msgBus->JoinSession(name, SERVICE_PORT, this, s_sessionId, opts);
            if (ER_OK != status) {
                printf("JoinSession failed (status=%s)\n", QCC_StatusText(status));
            } else {
                printf("JoinSession SUCCESS (Session id=%d)\n", s_sessionId);
            }
            s_joinComplete = true;
        }
    }

};

/*
 * This is the local implementation of the an AuthListener.  ECDHEKeyXListener is
 * designed to only handle ECDHE Key Exchange Authentication requests.
 *
 * If any other authMechanism is used other than ECDHE Key Exchange authentication
 * will fail.
 */
class ECDHEKeyXListener : public AuthListener {
  public:
    ECDHEKeyXListener()
    {
    }

    bool RequestCredentials(const char* authMechanism, const char* authPeer, uint16_t authCount, const char* userId, uint16_t credMask, Credentials& creds)
    {
        printf("RequestCredentials for authenticating peer name %s using mechanism %s authCount %d\n", authPeer, authMechanism, authCount);
        if (strcmp(authMechanism, KEYX_ECDHE_NULL) == 0) {
            creds.SetExpiration(100);  /* set the master secret expiry time to 100 seconds */
            return true;
        } else if (strcmp(authMechanism, KEYX_ECDHE_PSK) == 0) {
            /*
             * Solicit the Pre shared secret
             */
            if ((credMask& AuthListener::CRED_USER_NAME) == AuthListener::CRED_USER_NAME) {
                printf("RequestCredentials received psk ID %s\n", creds.GetUserName().c_str());
            }
            /*
             * Based on the pre shared secret id, the application can retrieve
             * the pre shared secret from storage or from the end user.
             * In this example, the pre shared secret is a hard coded string
             */
            String psk("123456");
            creds.SetPassword(psk);
            creds.SetExpiration(100);  /* set the master secret expiry time to 100 seconds */
            return true;
        } else if (strcmp(authMechanism, KEYX_ECDHE_ECDSA) == 0) {
            /* generate the private key and certificate */
            String privateKeyPEM;
            String certChainPEM;
            GenKeyAndSelfSignCert(privateKeyPEM, certChainPEM);
            if ((credMask& AuthListener::CRED_PRIVATE_KEY) == AuthListener::CRED_PRIVATE_KEY) {
                creds.SetPrivateKey(privateKeyPEM);
            }
            if ((credMask& AuthListener::CRED_CERT_CHAIN) == AuthListener::CRED_CERT_CHAIN) {
                creds.SetCertChain(certChainPEM);
            }
            creds.SetExpiration(100);  /* set the master secret expiry time to 100 seconds */
            return true;
        }
        return false;
    }

    bool VerifyCredentials(const char* authMechanism, const char* authPeer, const Credentials& creds)
    {
        /* only the ECDHE_ECDSA calls for peer credential verification */
        if (strcmp(authMechanism, KEYX_ECDHE_ECDSA) == 0) {
            if (creds.IsSet(AuthListener::CRED_CERT_CHAIN)) {
                /*
                 * AllJoyn sends back the certificate chain for the application to verify.
                 * The application has to option to verify the certificate
                 * chain.  If the cert chain is validated and trusted then return true; otherwise, return false.
                 */
                printf("VerifyCredentials receives cert chain %s\n", creds.GetCertChain().c_str());
            }
            return true;
        }
        return false;
    }

    void AuthenticationComplete(const char* authMechanism, const char* authPeer, bool success) {
        printf("SampleClientECDHE::AuthenticationComplete Authentication %s %s\n", authMechanism, success ? "successful" : "failed");
    }

  private:

    QStatus GenKeyAndSelfSignCert(String& privateKeyPEM, String& certPEM)
    {
        //Create a dsa key pair.
        Crypto_ECC ecc;
        ecc.GenerateDSAKeyPair();
        //Encode the private key to PEM
        QStatus status = CertificateX509::EncodePrivateKeyPEM((uint8_t*) ecc.GetDSAPrivateKey(), sizeof(ECCPrivateKey), privateKeyPEM);
        if (ER_OK != status) {
            return status;
        }
        //Generate a self-signed cert
        CertificateX509 cert;
        String issuerCN("Sample Code");

        cert.SetSerial("10001000");
        cert.SetIssuerCN((const uint8_t*) issuerCN.c_str(), issuerCN.size());
        cert.SetSubjectCN((const uint8_t*) issuerCN.c_str(), issuerCN.size());
        cert.SetSubjectPublicKey(ecc.GetDSAPublicKey());
        cert.SetCA(false);
        CertificateX509::ValidPeriod validity;
        validity.validFrom = qcc::GetEpochTimestamp() / 1000;
        validity.validTo = validity.validFrom + 7200;
        cert.SetValidity(&validity);
        status = cert.Sign(ecc.GetDSAPrivateKey());
        if (ER_OK != status) {
            return status;
        }
        certPEM = cert.GetPEM();
        return ER_OK;
    }
};

/** Static bus listener */
static MyBusListener g_busListener;
static char clientName[] = "Client%u";

void MakeClientName(void)
{
#ifndef CLIENT
#define CLIENT 0
#endif

    int client = CLIENT;

    // Prevent overwriting the client name buffer.
    if (client < 0 || client > 99) {
        client = 0;
    }

    sprintf(clientName, clientName, client);
}

/** Create the interface, report the result to stdout, and return the result status. */
QStatus CreateInterface(void)
{
    /* Add org.alljoyn.Bus.method_sample interface */
    InterfaceDescription* testIntf = NULL;
    QStatus status = g_msgBus->CreateInterface(INTERFACE_NAME, testIntf, AJ_IFC_SECURITY_REQUIRED);

    if (status == ER_OK) {
        printf("Interface '%s' created.\n", INTERFACE_NAME);
        testIntf->AddMethod("Ping", "s",  "s", "inStr,outStr", 0);
        testIntf->Activate();
    } else {
        printf("Failed to create interface '%s'.\n", INTERFACE_NAME);
    }

    return status;
}

/** Start the message bus, report the result to stdout, and return the result status. */
QStatus StartMessageBus(void)
{
    QStatus status = g_msgBus->Start();

    if (ER_OK == status) {
        printf("BusAttachment started.\n");
    } else {
        printf("BusAttachment::Start failed.\n");
    }

    return status;
}

/** Enable security, report the result to stdout, and return the result status. */
QStatus EnableSecurity()
{
    QCC_SetDebugLevel("ALLJOYN_AUTH", 3);
    QCC_SetDebugLevel("CRYPTO", 3);
    QCC_SetDebugLevel("AUTH_KEY_EXCHANGER", 3);

    /*
     * note the location of the keystore file has been specified and the
     * isShared parameter is being set to true. So this keystore file can
     * be used by multiple applications.
     */
    QStatus status = g_msgBus->EnablePeerSecurity(ECDHE_KEYX, new ECDHEKeyXListener(), "/.alljoyn_keystore/c_ecdhe.ks", true);

    if (ER_OK == status) {
        printf("BusAttachment::EnablePeerSecurity successful.\n");
    } else {
        printf("BusAttachment::EnablePeerSecurity failed (%s).\n", QCC_StatusText(status));
    }

    return status;
}

/** Handle the connection to the bus, report the result to stdout, and return the result status. */
QStatus ConnectToBus(void)
{
    QStatus status = g_msgBus->Connect();

    if (ER_OK == status) {
        printf("BusAttachment connected to '%s'.\n", g_msgBus->GetConnectSpec().c_str());
    } else {
        printf("BusAttachment::Connect('%s') failed.\n", g_msgBus->GetConnectSpec().c_str());
    }

    return status;
}

/** Register a bus listener in order to get discovery indications and report the event to stdout. */
void RegisterBusListener(void)
{
    /* Static bus listener */
    static MyBusListener s_busListener;

    g_msgBus->RegisterBusListener(s_busListener);
    printf("BusListener Registered.\n");
}

/** Begin discovery on the well-known name of the service to be called, report the result to
   stdout, and return the result status. */
QStatus FindAdvertisedName(void)
{
    /* Begin discovery on the well-known name of the service to be called */
    QStatus status = g_msgBus->FindAdvertisedName(SERVICE_NAME);

    if (status == ER_OK) {
        printf("org.alljoyn.Bus.FindAdvertisedName ('%s') succeeded.\n", SERVICE_NAME);
    } else {
        printf("org.alljoyn.Bus.FindAdvertisedName ('%s') failed (%s).\n", SERVICE_NAME, QCC_StatusText(status));
    }

    return status;
}

/** Wait for join session to complete, report the event to stdout, and return the result status. */
QStatus WaitForJoinSessionCompletion(void)
{
    unsigned int count = 0;

    while (!s_joinComplete && !s_interrupt) {
        if (0 == (count++ % 10)) {
            printf("Waited %u seconds for JoinSession completion.\n", count / 10);
        }

#ifdef _WIN32
        Sleep(100);
#else
        usleep(100 * 1000);
#endif
    }

    return s_joinComplete && !s_interrupt ? ER_OK : ER_ALLJOYN_JOINSESSION_REPLY_CONNECT_FAILED;
}

/** Do a method call, report the result to stdout, and return the result status. */
QStatus MakeMethodCall(void)
{
    ProxyBusObject remoteObj(*g_msgBus, SERVICE_NAME, SERVICE_PATH, s_sessionId);
    const InterfaceDescription* alljoynTestIntf = g_msgBus->GetInterface(INTERFACE_NAME);

    assert(alljoynTestIntf);
    remoteObj.AddInterface(*alljoynTestIntf);

    Message reply(*g_msgBus);
    MsgArg inputs[1];
    char buffer[80];

    sprintf(buffer, "%s says Hello AllJoyn!", clientName);

    inputs[0].Set("s", buffer);

    QStatus status = remoteObj.MethodCall(INTERFACE_NAME, "Ping", inputs, 1, reply, 5000);

    if (ER_OK == status) {
        printf("%s.Ping (path=%s) returned \"%s\".\n", INTERFACE_NAME,
               SERVICE_PATH, reply->GetArg(0)->v_string.str);
    } else {
        printf("MethodCall on %s.Ping failed.\n", INTERFACE_NAME);
    }

    return status;
}

/** Main entry point */
int main(int argc, char** argv, char** envArg)
{
    if (AllJoynInit() != ER_OK) {
        return 1;
    }
#ifdef ROUTER
    if (AllJoynRouterInit() != ER_OK) {
        AllJoynShutdown();
        return 1;
    }
#endif

    printf("AllJoyn Library version: %s.\n", ajn::GetVersion());
    printf("AllJoyn Library build info: %s.\n", ajn::GetBuildInfo());


    /* Install SIGINT handler */
    signal(SIGINT, SigIntHandler);

    MakeClientName();

    QStatus status = ER_OK;

    /* Create the application name. */
    char buffer[40];

    sprintf(buffer, "ECDHESecurity%s", clientName);

    /* Create message bus */
    g_msgBus = new BusAttachment(buffer, true);

    /* This test for NULL is only required if new() behavior is to return NULL
     * instead of throwing an exception upon an out of memory failure.
     */
    if (!g_msgBus) {
        status = ER_OUT_OF_MEMORY;
    }

    if (ER_OK == status) {
        status = CreateInterface();
    }

    if (ER_OK == status) {
        status = StartMessageBus();
    }

    if (ER_OK == status) {
        status = EnableSecurity();
    }

    if (ER_OK == status) {
        status = ConnectToBus();
    }

    if (ER_OK == status) {
        RegisterBusListener();
        status = FindAdvertisedName();
    }

    if (ER_OK == status) {
        status = WaitForJoinSessionCompletion();
    }

    if (ER_OK == status) {
        status = MakeMethodCall();
    }

    /* Deallocate bus */
    delete g_msgBus;
    g_msgBus = NULL;

    printf("Basic client exiting with status 0x%04x (%s).\n", status, QCC_StatusText(status));

#ifdef ROUTER
    AllJoynRouterShutdown();
#endif
    AllJoynShutdown();
    return (int) status;
}
