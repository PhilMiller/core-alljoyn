/******************************************************************************
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
#include <string>
#include <algorithm>
#include <gtest/gtest.h>
#include <time.h>
#include <alljoyn/AuthListener.h>
#include <alljoyn_c/BusAttachment.h>
#include <alljoyn_c/PermissionConfigurator.h>
#include <alljoyn_c/SecurityApplicationProxy.h>
#include <qcc/Thread.h>
#include <qcc/Util.h>
#include <qcc/XmlElement.h>
#include "ajTestCommon.h"
#include "InMemoryKeyStore.h"
#include "SecurityApplicationProxyTestHelper.h"

using namespace ajn;
using namespace qcc;
using namespace std;

#define WAIT_MS 5
#define CALLBACK_TIMEOUT_MS 2000
#define ACLS_INDEX 2

#define VALID_ALLOW_ALL_RULES \
    "<rules>" \
    "<node>" \
    "<interface>" \
    "<method>" \
    "<annotation name = \"org.alljoyn.Bus.Action\" value = \"Provide\"/>" \
    "<annotation name = \"org.alljoyn.Bus.Action\" value = \"Modify\"/>" \
    "</method>" \
    "<property>" \
    "<annotation name = \"org.alljoyn.Bus.Action\" value = \"Provide\"/>" \
    "<annotation name = \"org.alljoyn.Bus.Action\" value = \"Modify\"/>" \
    "<annotation name = \"org.alljoyn.Bus.Action\" value = \"Observe\"/>" \
    "</propert>" \
    "<signal>" \
    "<annotation name = \"org.alljoyn.Bus.Action\" value = \"Provide\"/>" \
    "<annotation name = \"org.alljoyn.Bus.Action\" value = \"Observe\"/>" \
    "</signal>" \
    "</interface>" \
    "</node>" \
    "</rules>"

#define GROUP_ID_PLACEHOLDER ":groupId"
#define GROUP_PUBKEY_PLACEHOLDER ":groupPubKey"

static AJ_PCSTR VALID_SECURITY_MANAGER_MANIFEST_TEMPLATE =
    "<manifest>"
    "<node>"
    "<interface>"
    "<method>"
    "<annotation name=\"org.alljoyn.Bus.Action\" value=\"Modify\"/>"
    "<annotation name=\"org.alljoyn.Bus.Action\" value=\"Provide\"/>"
    "</method>"
    "<property>"
    "<annotation name=\"org.alljoyn.Bus.Action\" value=\"Modify\"/>"
    "<annotation name=\"org.alljoyn.Bus.Action\" value=\"Provide\"/>"
    "<annotation name=\"org.alljoyn.Bus.Action\" value=\"Observe\"/>"
    "</property>"
    "<signal>"
    "<annotation name=\"org.alljoyn.Bus.Action\" value=\"Provide\"/>"
    "<annotation name=\"org.alljoyn.Bus.Action\" value=\"Observe\"/>"
    "</signal>"
    "</interface>"
    "</node>"
    "</manifest>";

static AJ_PCSTR VALID_MANAGED_APP_MANIFEST_TEMPLATE =
    "<manifest>"
    "<node name=\"/Node0\">"
    "<interface name=\"org.test.alljoyn.Interface\">"
    "<method name=\"MethodName\">"
    "<annotation name=\"org.alljoyn.Bus.Action\" value=\"Modify\"/>"
    "</method>"
    "</interface>"
    "</node>"
    "</manifest>";

static AJ_PCSTR VALID_NEWER_POLICY =
    "<policy>"
    "<policyVersion>1</policyVersion>"
    "<serialNumber>200</serialNumber>"
    "<acls>"
    "<acl>"
    "<peers>"
    "<peer>"
    "<type>ALL</type>"
    "</peer>"
    "</peers>"
    VALID_ALLOW_ALL_RULES
    "</acl>"
    "</acls>"
    "</policy>";

static AJ_PCSTR VALID_OLDER_POLICY =
    "<policy>"
    "<policyVersion>1</policyVersion>"
    "<serialNumber>100</serialNumber>"
    "<acls>"
    "<acl>"
    "<peers>"
    "<peer>"
    "<type>ALL</type>"
    "</peer>"
    "</peers>"
    VALID_ALLOW_ALL_RULES
    "</acl>"
    "</acls>"
    "</policy>";

static AJ_PCSTR DEFAULT_POLICY_FIX_FOR_ASACORE_2755 =
    "<acl>"
    "<peers>"
    "<peer>"
    "<type>WITH_MEMBERSHIP</type>"
    "<publicKey>" GROUP_PUBKEY_PLACEHOLDER "</publicKey>"
    "<sgID>" GROUP_ID_PLACEHOLDER "</sgID>"
    "</peer>"
    "</peers>"
    VALID_ALLOW_ALL_RULES
    "</acl>";

static AJ_PCSTR INVALID_MANIFEST =
    "<manifest>"
    "</manifest>";

class SecurityApplicationProxyPreProxyTest : public testing::Test {
  public:

    SecurityApplicationProxyPreProxyTest() :
        securityManagerSecurityApplicationProxy(nullptr),
        securityManagerIdentityCertificate(nullptr),
        securityManagerPublicKey(nullptr),
        securityManagerPrivateKey(nullptr),
        signedManifestXml(nullptr),
        defaultEcdheAuthListener((alljoyn_authlistener) new DefaultECDHEAuthListener())
    { }

    virtual void SetUp()
    {
        setUpCallbacks();

        securityManagerPermissionConfigurationListener = alljoyn_permissionconfigurationlistener_create(&callbacks, nullptr);
        basicBusSetup(&securityManager,
                      "securityManager",
                      &securityManagerKeyStore,
                      VALID_SECURITY_MANAGER_MANIFEST_TEMPLATE,
                      securityManagerPermissionConfigurationListener);

        securityManagerUniqueName = alljoyn_busattachment_getuniquename(securityManager);

        openManagementSession(securityManager, securityManagerUniqueName, &securityManagerSessionId);
        SecurityApplicationProxyTestHelper::CreateIdentityCert(securityManager, securityManager, &securityManagerIdentityCertificate);
        SecurityApplicationProxyTestHelper::RetrieveDSAPublicKeyFromKeyStore(securityManager, &securityManagerPublicKey);
        SecurityApplicationProxyTestHelper::RetrieveDSAPrivateKeyFromKeyStore(securityManager, &securityManagerPrivateKey);
    }

    virtual void TearDown()
    {
        if (nullptr != securityManagerSecurityApplicationProxy) {
            alljoyn_securityapplicationproxy_destroy(securityManagerSecurityApplicationProxy);
        }

        if (nullptr != signedManifestXml) {
            alljoyn_securityapplicationproxy_manifest_destroy(signedManifestXml);
        }

        delete[] securityManagerIdentityCertificate;
        delete[] securityManagerPublicKey;
        delete[] securityManagerPrivateKey;
        alljoyn_authlistenerasync_destroy(defaultEcdheAuthListener);

        alljoyn_permissionconfigurationlistener_destroy(securityManagerPermissionConfigurationListener);

        basicBusTearDown(securityManager);
    }

  protected:
    static bool policyChangeHappened;
    static bool factoryResetHappened;
    static bool startManagementHappened;
    static bool endManagementHappened;

    alljoyn_securityapplicationproxy securityManagerSecurityApplicationProxy;
    alljoyn_busattachment securityManager;
    alljoyn_sessionid securityManagerSessionId;
    alljoyn_permissionconfigurationlistener_callbacks callbacks;
    AJ_PCSTR securityManagerUniqueName;
    AJ_PSTR securityManagerIdentityCertificate;
    AJ_PSTR securityManagerPublicKey;
    AJ_PSTR securityManagerPrivateKey;
    AJ_PSTR signedManifestXml;

    void basicBusSetup(alljoyn_busattachment* bus,
                       AJ_PCSTR busName,
                       InMemoryKeyStoreListener* keyStoreListener,
                       AJ_PCSTR manifestTemplate,
                       alljoyn_permissionconfigurationlistener configurationListener)
    {
        *bus = alljoyn_busattachment_create(busName, QCC_FALSE);
        ASSERT_EQ(ER_OK, alljoyn_busattachment_start(*bus));
        ASSERT_EQ(ER_OK, alljoyn_busattachment_connect(*bus, getConnectArg().c_str()));
        ASSERT_EQ(ER_OK, alljoyn_busattachment_registerkeystorelistener(*bus, (alljoyn_keystorelistener)keyStoreListener));
        ASSERT_EQ(ER_OK, alljoyn_busattachment_enablepeersecuritywithpermissionconfigurationlistener(*bus,
                                                                                                     "ALLJOYN_ECDHE_NULL ALLJOYN_ECDHE_ECDSA",
                                                                                                     defaultEcdheAuthListener,
                                                                                                     nullptr,
                                                                                                     QCC_FALSE,
                                                                                                     configurationListener));
        setUpManifest(*bus, manifestTemplate);
    }

    void basicBusTearDown(const alljoyn_busattachment bus)
    {
        ASSERT_EQ(ER_OK, alljoyn_busattachment_stop(bus));
        ASSERT_EQ(ER_OK, alljoyn_busattachment_join(bus));
    }

    void openManagementSession(alljoyn_busattachment fromBus, AJ_PCSTR toBusUniqueName, alljoyn_sessionid* sessionId)
    {
        alljoyn_sessionopts sessionOpts = alljoyn_sessionopts_create(ALLJOYN_TRAFFIC_TYPE_MESSAGES,
                                                                     QCC_FALSE,
                                                                     ALLJOYN_PROXIMITY_ANY,
                                                                     ALLJOYN_TRANSPORT_ANY);
        ASSERT_EQ(ER_OK, alljoyn_busattachment_joinsession(fromBus,
                                                           toBusUniqueName,
                                                           PERMISSION_MANAGEMENT_SESSION_PORT,
                                                           nullptr,
                                                           sessionId,
                                                           sessionOpts));
    }

  private:

    alljoyn_authlistener defaultEcdheAuthListener;
    alljoyn_permissionconfigurationlistener securityManagerPermissionConfigurationListener;
    InMemoryKeyStoreListener securityManagerKeyStore;

    static void AJ_CALL policyChangedCallback(const void* context)
    {
        QCC_UNUSED(context);
        policyChangeHappened = true;
    }

    static QStatus AJ_CALL factoryResetCallback(const void* context)
    {
        QCC_UNUSED(context);
        factoryResetHappened = true;

        return ER_OK;
    }

    static void AJ_CALL startManagementCallback(const void* context)
    {
        QCC_UNUSED(context);
        startManagementHappened = true;
    }

    static void AJ_CALL endManagementCallback(const void* context)
    {
        QCC_UNUSED(context);
        endManagementHappened = true;
    }

    void setUpCallbacks()
    {
        memset(&callbacks, 0, sizeof(callbacks));
        callbacks.factory_reset = factoryResetCallback;
        callbacks.policy_changed = policyChangedCallback;
        callbacks.start_management = startManagementCallback;
        callbacks.end_management = endManagementCallback;

        policyChangeHappened = false;
        factoryResetHappened = false;
        startManagementHappened = false;
        endManagementHappened = false;
    }

    void setUpManifest(alljoyn_busattachment bus, AJ_PCSTR manifestTemplate)
    {
        alljoyn_permissionconfigurator configurator = alljoyn_busattachment_getpermissionconfigurator(bus);
        ASSERT_EQ(ER_OK, alljoyn_permissionconfigurator_setmanifestfromxml(configurator, manifestTemplate));
    }
};

class SecurityApplicationProxySelfClaimTest : public SecurityApplicationProxyPreProxyTest {
  public:
    SecurityApplicationProxySelfClaimTest() : SecurityApplicationProxyPreProxyTest()
    { }

    virtual void SetUp()
    {
        SecurityApplicationProxyPreProxyTest::SetUp();

        managedAppPermissionConfigurationListener = alljoyn_permissionconfigurationlistener_create(&callbacks, nullptr);
        basicBusSetup(&managedApp,
                      "managedApp",
                      &managedAppKeyStore,
                      VALID_MANAGED_APP_MANIFEST_TEMPLATE,
                      managedAppPermissionConfigurationListener);

        adminGroupId = adminGroupGuid.GetBytes();

        invalidProxy = alljoyn_securityapplicationproxy_create(securityManager, "someInvalidBusName", securityManagerSessionId);

        securityManagerSecurityApplicationProxy = alljoyn_securityapplicationproxy_create(securityManager, securityManagerUniqueName, securityManagerSessionId);

        ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_signmanifest(VALID_SECURITY_MANAGER_MANIFEST_TEMPLATE,
                                                                       securityManagerIdentityCertificate,
                                                                       securityManagerPrivateKey,
                                                                       &(signedSecurityManagerManifests[0])));
    }

    virtual void TearDown()
    {
        basicBusTearDown(managedApp);

        for (AJ_PSTR signedSecurityManagerManifest : signedSecurityManagerManifests) {
            delete[] signedSecurityManagerManifest;
        }

        alljoyn_permissionconfigurationlistener_destroy(managedAppPermissionConfigurationListener);

        SecurityApplicationProxyPreProxyTest::TearDown();
    }

  protected:
    const uint8_t* adminGroupId;
    alljoyn_securityapplicationproxy invalidProxy;
    alljoyn_busattachment managedApp;
    AJ_PSTR signedSecurityManagerManifests[1];
    GUID128 adminGroupGuid;

  private:
      alljoyn_permissionconfigurationlistener managedAppPermissionConfigurationListener;
    InMemoryKeyStoreListener managedAppKeyStore;
};

class SecurityApplicationProxyPreClaimTest : public SecurityApplicationProxySelfClaimTest {
  public:
    SecurityApplicationProxyPreClaimTest() : SecurityApplicationProxySelfClaimTest(),
        adminGroupMembershipCertificate(nullptr),
        managedAppIdentityCertificate(nullptr),
        managedAppIdentityCertificateChain(nullptr),
        retrievedManagedAppManifestTemplate(nullptr),
        retrievedManagedAppEccPublicKey(nullptr),
        managedAppSecurityApplicationProxy(nullptr)
    { }

    virtual void SetUp()
    {
        SecurityApplicationProxySelfClaimTest::SetUp();

        selfClaimSecurityManager();
        getAdminGroupMembershipCertificate();
        getManagedAppSecurityApplicationProxy();
        getManagedAppIdentityCertAndManifest();
    }

    virtual void TearDown()
    {
        if (nullptr != retrievedManagedAppManifestTemplate) {
            alljoyn_securityapplicationproxy_manifesttemplate_destroy(retrievedManagedAppManifestTemplate);
        }

        if (nullptr != retrievedManagedAppEccPublicKey) {
            alljoyn_securityapplicationproxy_eccpublickey_destroy(retrievedManagedAppEccPublicKey);
        }

        alljoyn_securityapplicationproxy_destroy(managedAppSecurityApplicationProxy);

        delete[] managedAppIdentityCertificateChain;

        delete[] adminGroupMembershipCertificate;

        for (AJ_PSTR signedManagedAppManifest : managedAppSignedManifests) {
            delete[] signedManagedAppManifest;
        }

        SecurityApplicationProxySelfClaimTest::TearDown();
    }

  protected:
    AJ_PSTR adminGroupMembershipCertificate;
    AJ_PSTR managedAppIdentityCertificate;
    AJ_PSTR managedAppIdentityCertificateChain;
    AJ_PSTR retrievedManagedAppManifestTemplate;
    AJ_PSTR retrievedManagedAppEccPublicKey;
    AJ_PSTR managedAppSignedManifests[1];
    alljoyn_securityapplicationproxy managedAppSecurityApplicationProxy;
    alljoyn_applicationstate retrievedManagedAppApplicationState;
    alljoyn_claimcapabilities retrievedManagedAppClaimCapabilities;
    alljoyn_claimcapabilities retrievedManagedAppClaimCapabilitiesAdditionalInfo;

    string removeNewLines(string input)
    {
        input.erase(remove(input.begin(), input.end(), '\n'), input.end());
        return input;
    }

    bool waitForTrueOrTimeout(bool* isTrue)
    {
        time_t startTime = time(nullptr);
        int timeDiffMs = difftime(time(nullptr), startTime) * 1000;
        while (!(*isTrue)
               && (timeDiffMs < CALLBACK_TIMEOUT_MS)) {
            qcc::Sleep(WAIT_MS);
            timeDiffMs = difftime(time(nullptr), startTime) * 1000;
        }

        return *isTrue;
    }

  private:
    alljoyn_sessionid managedAppSessionId;
    AJ_PCSTR managedAppUniqueName;

    void selfClaimSecurityManager()
    {
        ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_claim(securityManagerSecurityApplicationProxy,
                                                                securityManagerPublicKey,
                                                                securityManagerIdentityCertificate,
                                                                adminGroupId,
                                                                GUID128::SIZE,
                                                                securityManagerPublicKey,
                                                                (AJ_PCSTR*)signedSecurityManagerManifests,
                                                                1U));
    }

    void getAdminGroupMembershipCertificate()
    {
        SecurityApplicationProxyTestHelper::CreateMembershipCert(securityManager,
                                                                 securityManager,
                                                                 adminGroupId,
                                                                 true,
                                                                 &adminGroupMembershipCertificate);
    }

    void getManagedAppSecurityApplicationProxy()
    {
        managedAppUniqueName = alljoyn_busattachment_getuniquename(managedApp);

        openManagementSession(securityManager, managedAppUniqueName, &managedAppSessionId);
        managedAppSecurityApplicationProxy = alljoyn_securityapplicationproxy_create(securityManager, managedAppUniqueName, managedAppSessionId);
    }

    void getManagedAppIdentityCertAndManifest()
    {
        SecurityApplicationProxyTestHelper::CreateIdentityCert(securityManager,
                                                               managedApp,
                                                               &managedAppIdentityCertificate,
                                                               false);
        ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_signmanifest(VALID_MANAGED_APP_MANIFEST_TEMPLATE,
                                                                       managedAppIdentityCertificate,
                                                                       securityManagerPrivateKey,
                                                                       &(managedAppSignedManifests[0])));
    }
};

class SecurityApplicationProxyPostClaimTest : public SecurityApplicationProxyPreClaimTest {
  public:
    SecurityApplicationProxyPostClaimTest() : SecurityApplicationProxyPreClaimTest()
    { }

    virtual void SetUp()
    {
        SecurityApplicationProxyPreClaimTest::SetUp();
        claimManagedApp();
    }

  private:

    void claimManagedApp()
    {
        SecurityApplicationProxyTestHelper::CreateIdentityCertChain(securityManagerIdentityCertificate, managedAppIdentityCertificate, &managedAppIdentityCertificateChain);
        ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_claim(managedAppSecurityApplicationProxy,
                                                                securityManagerPublicKey,
                                                                managedAppIdentityCertificateChain,
                                                                adminGroupId,
                                                                GUID128::SIZE,
                                                                securityManagerPublicKey,
                                                                (AJ_PCSTR*)managedAppSignedManifests,
                                                                1U));
    }
};

class SecurityApplicationProxyFullSetupTest : public SecurityApplicationProxyPostClaimTest {
  public:
    SecurityApplicationProxyFullSetupTest() : SecurityApplicationProxyPostClaimTest(),
        oldPolicy(nullptr),
        newPolicy(nullptr)
    { }

    virtual void SetUp()
    {
        SecurityApplicationProxyPostClaimTest::SetUp();
        installAdminGroupMembership();

        setUpPolicies();
    }

    virtual void TearDown()
    {
        delete[] newPolicy;
        delete[] oldPolicy;

        SecurityApplicationProxyPostClaimTest::TearDown();
    }

  protected:

    AJ_PSTR oldPolicy;
    AJ_PSTR newPolicy;

    void modifyManagedAppIdentityCertAndManifests()
    {
        delete[] managedAppIdentityCertificate;
        delete[] managedAppSignedManifests[0];

        SecurityApplicationProxyTestHelper::CreateIdentityCert(securityManager,
                                                               managedApp,
                                                               &managedAppIdentityCertificate,
                                                               true);

        ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_signmanifest(VALID_SECURITY_MANAGER_MANIFEST_TEMPLATE,
                                                                       managedAppIdentityCertificate,
                                                                       securityManagerPrivateKey,
                                                                       &(managedAppSignedManifests[0])));
    }

  private:

    void setUpPolicies()
    {
        updatePolicyWithTrustAnchor(VALID_OLDER_POLICY, &oldPolicy);
        updatePolicyWithTrustAnchor(VALID_NEWER_POLICY, &newPolicy);
    }

    /**
     * A fix for ASACORE-2755
     */
    void updatePolicyWithTrustAnchor(AJ_PCSTR originalPolicy, AJ_PSTR* fixedPolicy)
    {
        XmlElement* fixedPolicyXml = nullptr;
        XmlElement* fixXml = nullptr;
        string policyFixTemplate = DEFAULT_POLICY_FIX_FOR_ASACORE_2755;

        SecurityApplicationProxyTestHelper::ReplaceString(policyFixTemplate, GROUP_PUBKEY_PLACEHOLDER, securityManagerPublicKey);
        SecurityApplicationProxyTestHelper::ReplaceString(policyFixTemplate, GROUP_ID_PLACEHOLDER, adminGroupGuid.ToString().c_str());

        ASSERT_EQ(ER_OK, XmlElement::GetRoot(originalPolicy, &fixedPolicyXml));
        ASSERT_EQ(ER_OK, XmlElement::GetRoot(policyFixTemplate.c_str(), &fixXml));

        fixedPolicyXml->GetChildren()[ACLS_INDEX]->AddChild(fixXml);

        SecurityApplicationProxyTestHelper::String2CString(fixedPolicyXml->Generate(), fixedPolicy);
        delete fixedPolicyXml;
    }


    void installAdminGroupMembership()
    {
        ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_installmembership(securityManagerSecurityApplicationProxy, adminGroupMembershipCertificate));
        ASSERT_EQ(ER_OK, alljoyn_proxybusobject_secureconnection((alljoyn_proxybusobject)managedAppSecurityApplicationProxy, true));
    }
};

bool SecurityApplicationProxyPreProxyTest::policyChangeHappened;
bool SecurityApplicationProxyPreProxyTest::factoryResetHappened;
bool SecurityApplicationProxyPreProxyTest::startManagementHappened;
bool SecurityApplicationProxyPreProxyTest::endManagementHappened;

TEST_F(SecurityApplicationProxyPreProxyTest, shouldPassWhenCreatingWithNonExistingRemoteApp)
{
    EXPECT_NE(nullptr, alljoyn_securityapplicationproxy_create(securityManager, "someInvalidBusName", securityManagerSessionId));
}

TEST_F(SecurityApplicationProxyPreProxyTest, shouldPassWhenCreatingWithInvalidSessionId)
{
    EXPECT_NE(nullptr, alljoyn_securityapplicationproxy_create(securityManager, securityManagerUniqueName, 0));
}

TEST_F(SecurityApplicationProxyPreProxyTest, shouldPassWhenDestroyingNull)
{
    alljoyn_securityapplicationproxy_destroy(nullptr);
}

TEST_F(SecurityApplicationProxyPreProxyTest, shouldPassCreationgWithValidInput)
{
    securityManagerSecurityApplicationProxy = alljoyn_securityapplicationproxy_create(securityManager, securityManagerUniqueName, securityManagerSessionId);

    EXPECT_NE(nullptr, securityManagerSecurityApplicationProxy);
}

TEST_F(SecurityApplicationProxyPreProxyTest, shouldPassDestroyingValidProxy)
{
    securityManagerSecurityApplicationProxy = alljoyn_securityapplicationproxy_create(securityManager, securityManagerUniqueName, securityManagerSessionId);
    ASSERT_NE(nullptr, securityManagerSecurityApplicationProxy);

    alljoyn_securityapplicationproxy_destroy(securityManagerSecurityApplicationProxy);
    securityManagerSecurityApplicationProxy = nullptr;
}

TEST_F(SecurityApplicationProxyPreProxyTest, shouldReturnErrorWhenSigningInvalidManifest)
{
    EXPECT_EQ(ER_XML_MALFORMED, alljoyn_securityapplicationproxy_signmanifest(INVALID_MANIFEST,
                                                                              securityManagerIdentityCertificate,
                                                                              securityManagerPrivateKey,
                                                                              &signedManifestXml));
}

#ifdef NDEBUG
TEST_F(SecurityApplicationProxyPreProxyTest, shouldReturnErrorWhenSigningManifestWithNullCertificate)
{
    EXPECT_EQ(ER_INVALID_DATA, alljoyn_securityapplicationproxy_signmanifest(VALID_SECURITY_MANAGER_MANIFEST_TEMPLATE,
                                                                             nullptr,
                                                                             securityManagerPrivateKey,
                                                                             &signedManifestXml));
}

TEST_F(SecurityApplicationProxyPreProxyTest, shouldReturnErrorWhenSigningManifestWithNullPrivateKey)
{
    EXPECT_EQ(ER_INVALID_DATA, alljoyn_securityapplicationproxy_signmanifest(VALID_SECURITY_MANAGER_MANIFEST_TEMPLATE,
                                                                             securityManagerIdentityCertificate,
                                                                             nullptr,
                                                                             &signedManifestXml));
}

TEST_F(SecurityApplicationProxySelfClaimTest, shouldReturnErrorWhenClaimingWithNullPublicKey)
{
    EXPECT_EQ(ER_INVALID_DATA, alljoyn_securityapplicationproxy_claim(securityManagerSecurityApplicationProxy,
                                                                      nullptr,
                                                                      securityManagerIdentityCertificate,
                                                                      adminGroupId,
                                                                      GUID128::SIZE,
                                                                      securityManagerPublicKey,
                                                                      (AJ_PCSTR*)signedSecurityManagerManifests,
                                                                      1U));
}

TEST_F(SecurityApplicationProxySelfClaimTest, shouldReturnErrorWhenClaimingWithNullCertificate)
{
    EXPECT_EQ(ER_INVALID_DATA, alljoyn_securityapplicationproxy_claim(securityManagerSecurityApplicationProxy,
                                                                      securityManagerPublicKey,
                                                                      nullptr,
                                                                      adminGroupId,
                                                                      GUID128::SIZE,
                                                                      securityManagerPublicKey,
                                                                      (AJ_PCSTR*)signedSecurityManagerManifests,
                                                                      1U));
}

TEST_F(SecurityApplicationProxySelfClaimTest, shouldReturnErrorWhenClaimingWithNullGroupAuthority)
{
    EXPECT_EQ(ER_INVALID_DATA, alljoyn_securityapplicationproxy_claim(securityManagerSecurityApplicationProxy,
                                                                      securityManagerPublicKey,
                                                                      securityManagerIdentityCertificate,
                                                                      adminGroupId,
                                                                      GUID128::SIZE,
                                                                      nullptr,
                                                                      (AJ_PCSTR*)signedSecurityManagerManifests,
                                                                      1U));
}
#endif

TEST_F(SecurityApplicationProxyPreProxyTest, shouldReturnErrorWhenSigningManifestWithPublicKey)
{
    EXPECT_EQ(ER_INVALID_DATA, alljoyn_securityapplicationproxy_signmanifest(VALID_SECURITY_MANAGER_MANIFEST_TEMPLATE,
                                                                             securityManagerIdentityCertificate,
                                                                             securityManagerPublicKey,
                                                                             &signedManifestXml));
}

TEST_F(SecurityApplicationProxyPreProxyTest, shouldPassWhenSigningManifestWithValidInput)
{
    EXPECT_EQ(ER_OK, alljoyn_securityapplicationproxy_signmanifest(VALID_SECURITY_MANAGER_MANIFEST_TEMPLATE,
                                                                   securityManagerIdentityCertificate,
                                                                   securityManagerPrivateKey,
                                                                   &signedManifestXml));
}

TEST_F(SecurityApplicationProxyPreProxyTest, shouldPassWhenDestroyingNullSignedManifest)
{
    alljoyn_securityapplicationproxy_manifesttemplate_destroy(nullptr);
}

TEST_F(SecurityApplicationProxyPreProxyTest, shouldPassWhenDestroyingSignedManifest)
{
    ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_signmanifest(VALID_SECURITY_MANAGER_MANIFEST_TEMPLATE,
                                                                   securityManagerIdentityCertificate,
                                                                   securityManagerPrivateKey,
                                                                   &signedManifestXml));

    alljoyn_securityapplicationproxy_manifesttemplate_destroy(signedManifestXml);
    signedManifestXml = nullptr;
}

TEST_F(SecurityApplicationProxySelfClaimTest, shouldReturnErrorWhenClaimingWithInvalidProxy)
{
    EXPECT_EQ(ER_AUTH_FAIL, alljoyn_securityapplicationproxy_claim(invalidProxy,
                                                                   securityManagerPublicKey,
                                                                   securityManagerIdentityCertificate,
                                                                   adminGroupId,
                                                                   GUID128::SIZE,
                                                                   securityManagerPublicKey,
                                                                   (AJ_PCSTR*)signedSecurityManagerManifests,
                                                                   1U));
}

TEST_F(SecurityApplicationProxySelfClaimTest, shouldReturnErrorWhenClaimingWithInvalidPublicKey)
{
    AJ_PCSTR invalidPublicKey = securityManagerPrivateKey;
    EXPECT_EQ(ER_INVALID_DATA, alljoyn_securityapplicationproxy_claim(securityManagerSecurityApplicationProxy,
                                                                      invalidPublicKey,
                                                                      securityManagerIdentityCertificate,
                                                                      adminGroupId,
                                                                      GUID128::SIZE,
                                                                      securityManagerPublicKey,
                                                                      (AJ_PCSTR*)signedSecurityManagerManifests,
                                                                      1U));
}

TEST_F(SecurityApplicationProxySelfClaimTest, shouldReturnErrorWhenClaimingWithInvalidCertificate)
{
    AJ_PCSTR invalidIdentityCert = securityManagerPrivateKey;
    EXPECT_EQ(ER_INVALID_DATA, alljoyn_securityapplicationproxy_claim(securityManagerSecurityApplicationProxy,
                                                                      securityManagerPublicKey,
                                                                      invalidIdentityCert,
                                                                      adminGroupId,
                                                                      GUID128::SIZE,
                                                                      securityManagerPublicKey,
                                                                      (AJ_PCSTR*)signedSecurityManagerManifests,
                                                                      1U));
}

TEST_F(SecurityApplicationProxySelfClaimTest, shouldIgnoreAndPassWhenClaimingWithInvalidGroupId)
{
    uint8_t invalidGroupId[] = { 1 };
    EXPECT_EQ(ER_OK, alljoyn_securityapplicationproxy_claim(securityManagerSecurityApplicationProxy,
                                                            securityManagerPublicKey,
                                                            securityManagerIdentityCertificate,
                                                            invalidGroupId,
                                                            GUID128::SIZE,
                                                            securityManagerPublicKey,
                                                            (AJ_PCSTR*)signedSecurityManagerManifests,
                                                            1U));
}

TEST_F(SecurityApplicationProxySelfClaimTest, shouldReturnErrorWhenClaimingWithInvalidGroupIdSize)
{
    size_t invalidGroupIdSize = GUID128::SIZE + 1;
    EXPECT_EQ(ER_INVALID_GUID, alljoyn_securityapplicationproxy_claim(securityManagerSecurityApplicationProxy,
                                                                      securityManagerPublicKey,
                                                                      securityManagerIdentityCertificate,
                                                                      adminGroupId,
                                                                      invalidGroupIdSize,
                                                                      securityManagerPublicKey,
                                                                      (AJ_PCSTR*)signedSecurityManagerManifests,
                                                                      1U));
}

TEST_F(SecurityApplicationProxySelfClaimTest, shouldReturnErrorWhenClaimingWithInvalidGroupAuthority)
{
    AJ_PCSTR invalidGroupAuthority = securityManagerPrivateKey;
    EXPECT_EQ(ER_INVALID_DATA, alljoyn_securityapplicationproxy_claim(securityManagerSecurityApplicationProxy,
                                                                      securityManagerPublicKey,
                                                                      securityManagerIdentityCertificate,
                                                                      adminGroupId,
                                                                      GUID128::SIZE,
                                                                      invalidGroupAuthority,
                                                                      (AJ_PCSTR*)signedSecurityManagerManifests,
                                                                      1U));
}

TEST_F(SecurityApplicationProxySelfClaimTest, shouldReturnErrorWhenClaimingWithIdentityCertificateThumbprintMismatch)
{
    AJ_PSTR differentIdentityCertificate;
    SecurityApplicationProxyTestHelper::CreateIdentityCert(securityManager, managedApp, &differentIdentityCertificate);

    EXPECT_EQ(ER_UNKNOWN_CERTIFICATE, alljoyn_securityapplicationproxy_claim(securityManagerSecurityApplicationProxy,
                                                                             securityManagerPublicKey,
                                                                             differentIdentityCertificate,
                                                                             adminGroupId,
                                                                             GUID128::SIZE,
                                                                             securityManagerPublicKey,
                                                                             (AJ_PCSTR*)signedSecurityManagerManifests,
                                                                             1U));

    delete[] differentIdentityCertificate;
}

TEST_F(SecurityApplicationProxySelfClaimTest, shouldPassWhenClaimingWithValidInput)
{
    EXPECT_EQ(ER_OK, alljoyn_securityapplicationproxy_claim(securityManagerSecurityApplicationProxy,
                                                            securityManagerPublicKey,
                                                            securityManagerIdentityCertificate,
                                                            adminGroupId,
                                                            GUID128::SIZE,
                                                            securityManagerPublicKey,
                                                            (AJ_PCSTR*)signedSecurityManagerManifests,
                                                            1U));
}

TEST_F(SecurityApplicationProxyPreClaimTest, shouldReturnErrorWhileGettingApplicationStateWithInvalidProxy)
{
    EXPECT_EQ(ER_AUTH_FAIL, alljoyn_securityapplicationproxy_getapplicationstate(invalidProxy, &retrievedManagedAppApplicationState));
}

TEST_F(SecurityApplicationProxyPreClaimTest, shouldPassGetApplicationState)
{
    EXPECT_EQ(ER_OK, alljoyn_securityapplicationproxy_getapplicationstate(managedAppSecurityApplicationProxy, &retrievedManagedAppApplicationState));
}

TEST_F(SecurityApplicationProxyPreClaimTest, shouldGetClaimableApplicationState)
{
    ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_getapplicationstate(managedAppSecurityApplicationProxy, &retrievedManagedAppApplicationState));

    EXPECT_EQ(CLAIMABLE, retrievedManagedAppApplicationState);
}

TEST_F(SecurityApplicationProxyPreClaimTest, shouldReturnErrorWhileGettingClaimCapabilitiesWithInvalidProxy)
{
    EXPECT_EQ(ER_AUTH_FAIL, alljoyn_securityapplicationproxy_getclaimcapabilities(invalidProxy, &retrievedManagedAppClaimCapabilities));
}

TEST_F(SecurityApplicationProxyPreClaimTest, shouldPassGetClaimCapabilities)
{
    EXPECT_EQ(ER_OK, alljoyn_securityapplicationproxy_getclaimcapabilities(managedAppSecurityApplicationProxy, &retrievedManagedAppClaimCapabilities));
}

TEST_F(SecurityApplicationProxyPreClaimTest, shouldGetDefaultClaimCapabilities)
{
    ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_getclaimcapabilities(managedAppSecurityApplicationProxy, &retrievedManagedAppClaimCapabilities));

    EXPECT_EQ(CLAIM_CAPABILITIES_DEFAULT, retrievedManagedAppClaimCapabilities);
}

TEST_F(SecurityApplicationProxyPreClaimTest, shouldReturnErrorWhileGettingClaimCapabilitiesAdditionalInfoWithInvalidProxy)
{
    EXPECT_EQ(ER_AUTH_FAIL, alljoyn_securityapplicationproxy_getclaimcapabilitiesadditionalinfo(invalidProxy, &retrievedManagedAppClaimCapabilitiesAdditionalInfo));
}

TEST_F(SecurityApplicationProxyPreClaimTest, shouldPassGetClaimCapabilitiesAdditionalInfo)
{
    EXPECT_EQ(ER_OK, alljoyn_securityapplicationproxy_getclaimcapabilitiesadditionalinfo(managedAppSecurityApplicationProxy, &retrievedManagedAppClaimCapabilitiesAdditionalInfo));
}

TEST_F(SecurityApplicationProxyPreClaimTest, shouldGetDefaultClaimCapabilitiesAdditionalInfo)
{
    ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_getclaimcapabilitiesadditionalinfo(managedAppSecurityApplicationProxy, &retrievedManagedAppClaimCapabilitiesAdditionalInfo));

    EXPECT_EQ(0, retrievedManagedAppClaimCapabilitiesAdditionalInfo);
}

TEST_F(SecurityApplicationProxyPreClaimTest, shouldReturnErrorWhileGettingManifestTemplateWithInvalidProxy)
{
    EXPECT_EQ(ER_AUTH_FAIL, alljoyn_securityapplicationproxy_getmanifesttemplate(invalidProxy, &retrievedManagedAppManifestTemplate));
}

TEST_F(SecurityApplicationProxyPreClaimTest, shouldPassGetManifestTemplate)
{
    EXPECT_EQ(ER_OK, alljoyn_securityapplicationproxy_getmanifesttemplate(managedAppSecurityApplicationProxy, &retrievedManagedAppManifestTemplate));
}

TEST_F(SecurityApplicationProxyPreClaimTest, shouldGetValidManifestTemplate)
{
    ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_getmanifesttemplate(managedAppSecurityApplicationProxy, &retrievedManagedAppManifestTemplate));

    EXPECT_STREQ(VALID_MANAGED_APP_MANIFEST_TEMPLATE, removeNewLines(retrievedManagedAppManifestTemplate).c_str());
}

TEST_F(SecurityApplicationProxyPreClaimTest, shouldDestroyNullManifestTemplate)
{
    alljoyn_securityapplicationproxy_manifesttemplate_destroy(nullptr);
}

TEST_F(SecurityApplicationProxyPreClaimTest, shouldDestroyRetrievedManifestTemplate)
{
    ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_getmanifesttemplate(managedAppSecurityApplicationProxy, &retrievedManagedAppManifestTemplate));

    alljoyn_securityapplicationproxy_manifesttemplate_destroy(retrievedManagedAppManifestTemplate);
    retrievedManagedAppManifestTemplate = nullptr;
}

TEST_F(SecurityApplicationProxyPreClaimTest, shouldReturnErrorWhileGettingEccPublicKeyWithInvalidProxy)
{
    EXPECT_EQ(ER_AUTH_FAIL, alljoyn_securityapplicationproxy_geteccpublickey(invalidProxy, &retrievedManagedAppEccPublicKey));
}

TEST_F(SecurityApplicationProxyPreClaimTest, shouldPassGetEccPublicKey)
{
    EXPECT_EQ(ER_OK, alljoyn_securityapplicationproxy_geteccpublickey(managedAppSecurityApplicationProxy, &retrievedManagedAppEccPublicKey));
}

TEST_F(SecurityApplicationProxyPreClaimTest, shouldGetValidEccPublicKey)
{
    ECCPublicKey publicKey;
    ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_geteccpublickey(managedAppSecurityApplicationProxy, &retrievedManagedAppEccPublicKey));

    EXPECT_EQ(ER_OK, CertificateX509::DecodePublicKeyPEM(retrievedManagedAppEccPublicKey, &publicKey));
}

TEST_F(SecurityApplicationProxyPreClaimTest, shouldDestroyNullEccPublicKey)
{
    alljoyn_securityapplicationproxy_manifesttemplate_destroy(nullptr);
}

TEST_F(SecurityApplicationProxyPreClaimTest, shouldDestroyRetrievedEccPublicKey)
{
    ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_geteccpublickey(managedAppSecurityApplicationProxy, &retrievedManagedAppEccPublicKey));

    alljoyn_securityapplicationproxy_eccpublickey_destroy(retrievedManagedAppEccPublicKey);
    retrievedManagedAppEccPublicKey = nullptr;
}

TEST_F(SecurityApplicationProxyPreClaimTest, shouldPassClaimManagedAppWithIdentityCertChainHavingOneCert)
{
    EXPECT_EQ(ER_OK, alljoyn_securityapplicationproxy_claim(managedAppSecurityApplicationProxy,
                                                            securityManagerPublicKey,
                                                            managedAppIdentityCertificate,
                                                            adminGroupId,
                                                            GUID128::SIZE,
                                                            securityManagerPublicKey,
                                                            (AJ_PCSTR*)managedAppSignedManifests,
                                                            1U));
}

TEST_F(SecurityApplicationProxyPreClaimTest, shouldPassClaimManagedAppWithIdentityCertChainHavingTwoCerts)
{
    SecurityApplicationProxyTestHelper::CreateIdentityCertChain(securityManagerIdentityCertificate, managedAppIdentityCertificate, &managedAppIdentityCertificateChain);

    EXPECT_EQ(ER_OK, alljoyn_securityapplicationproxy_claim(managedAppSecurityApplicationProxy,
                                                            securityManagerPublicKey,
                                                            managedAppIdentityCertificateChain,
                                                            adminGroupId,
                                                            GUID128::SIZE,
                                                            securityManagerPublicKey,
                                                            (AJ_PCSTR*)managedAppSignedManifests,
                                                            1U));
}

TEST_F(SecurityApplicationProxyPreClaimTest, shouldChangeAppStateAfterClaimingManagedAppWithIdentityCertChainHavingTwoCerts)
{
    SecurityApplicationProxyTestHelper::CreateIdentityCertChain(securityManagerIdentityCertificate, managedAppIdentityCertificate, &managedAppIdentityCertificateChain);
    ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_claim(managedAppSecurityApplicationProxy,
                                                            securityManagerPublicKey,
                                                            managedAppIdentityCertificateChain,
                                                            adminGroupId,
                                                            GUID128::SIZE,
                                                            securityManagerPublicKey,
                                                            (AJ_PCSTR*)managedAppSignedManifests,
                                                            1U));
    ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_getapplicationstate(managedAppSecurityApplicationProxy, &retrievedManagedAppApplicationState));

    EXPECT_EQ(CLAIMED, retrievedManagedAppApplicationState);
}

TEST_F(SecurityApplicationProxyPreClaimTest, shouldCallPolicyChangedCallbackAfterClaim)
{
    policyChangeHappened = false;
    SecurityApplicationProxyTestHelper::CreateIdentityCertChain(securityManagerIdentityCertificate, managedAppIdentityCertificate, &managedAppIdentityCertificateChain);
    ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_claim(managedAppSecurityApplicationProxy,
                                                            securityManagerPublicKey,
                                                            managedAppIdentityCertificateChain,
                                                            adminGroupId,
                                                            GUID128::SIZE,
                                                            securityManagerPublicKey,
                                                            (AJ_PCSTR*)managedAppSignedManifests,
                                                            1U));

    EXPECT_TRUE(waitForTrueOrTimeout(&policyChangeHappened));
}

TEST_F(SecurityApplicationProxyPreClaimTest, shouldPassInstallMembership)
{
    EXPECT_EQ(ER_OK, alljoyn_securityapplicationproxy_installmembership(securityManagerSecurityApplicationProxy, adminGroupMembershipCertificate));
}

TEST_F(SecurityApplicationProxyPreClaimTest, shouldReturnErrorWhenInstallingSameMembershipTwice)
{
    ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_installmembership(securityManagerSecurityApplicationProxy, adminGroupMembershipCertificate));

    EXPECT_EQ(ER_DUPLICATE_CERTIFICATE, alljoyn_securityapplicationproxy_installmembership(securityManagerSecurityApplicationProxy, adminGroupMembershipCertificate));
}

TEST_F(SecurityApplicationProxyPostClaimTest, shouldReturnErrorForStartManagementWithInvalidProxy)
{
    EXPECT_EQ(ER_AUTH_FAIL, alljoyn_securityapplicationproxy_startmanagement(invalidProxy));
}

TEST_F(SecurityApplicationProxyPostClaimTest, shouldReturnErrorForStartManagementForValidProxyButMissingMembership)
{
    EXPECT_EQ(ER_PERMISSION_DENIED, alljoyn_securityapplicationproxy_startmanagement(managedAppSecurityApplicationProxy));
}

TEST_F(SecurityApplicationProxyPostClaimTest, shouldReturnErrorForEndManagementWithInvalidProxy)
{
    EXPECT_EQ(ER_AUTH_FAIL, alljoyn_securityapplicationproxy_endmanagement(invalidProxy));
}

TEST_F(SecurityApplicationProxyPostClaimTest, shouldReturnErrorForEndManagementCallForValidProxyButMissingMembership)
{
    EXPECT_EQ(ER_PERMISSION_DENIED, alljoyn_securityapplicationproxy_endmanagement(managedAppSecurityApplicationProxy));
}

TEST_F(SecurityApplicationProxyPostClaimTest, shouldReturnErrorForResetWithInvalidProxy)
{
    EXPECT_EQ(ER_AUTH_FAIL, alljoyn_securityapplicationproxy_reset(invalidProxy));
}

TEST_F(SecurityApplicationProxyPostClaimTest, shouldReturnErrorForResetForValidProxyButMissingMembership)
{
    EXPECT_EQ(ER_PERMISSION_DENIED, alljoyn_securityapplicationproxy_reset(managedAppSecurityApplicationProxy));
}

TEST_F(SecurityApplicationProxyPostClaimTest, shouldReturnPolicyErrorForResetWithInvalidProxy)
{
    EXPECT_EQ(ER_AUTH_FAIL, alljoyn_securityapplicationproxy_resetpolicy(invalidProxy));
}

TEST_F(SecurityApplicationProxyPostClaimTest, shouldReturnErrorForResetPolicyForValidProxyButMissingMembership)
{
    EXPECT_EQ(ER_PERMISSION_DENIED, alljoyn_securityapplicationproxy_resetpolicy(managedAppSecurityApplicationProxy));
}

TEST_F(SecurityApplicationProxyPostClaimTest, shouldReturnErrorForUpdatePolicyWithInvalidProxy)
{
    EXPECT_EQ(ER_AUTH_FAIL, alljoyn_securityapplicationproxy_updatepolicy(invalidProxy, VALID_NEWER_POLICY));
}

TEST_F(SecurityApplicationProxyPostClaimTest, shouldReturnErrorForUpdatePolicyForValidProxyButMissingMembership)
{
    EXPECT_EQ(ER_PERMISSION_DENIED, alljoyn_securityapplicationproxy_updatepolicy(managedAppSecurityApplicationProxy, VALID_NEWER_POLICY));
}

TEST_F(SecurityApplicationProxyPostClaimTest, shouldReturnErrorForUpdateIdentityWithInvalidProxy)
{
    EXPECT_EQ(ER_AUTH_FAIL, alljoyn_securityapplicationproxy_updateidentity(invalidProxy,
                                                                            managedAppIdentityCertificateChain,
                                                                            (AJ_PCSTR*)managedAppSignedManifests,
                                                                            1U));
}

TEST_F(SecurityApplicationProxyPostClaimTest, shouldReturnErrorForUpdateIdentityForValidProxyButMissingMembership)
{
    EXPECT_EQ(ER_PERMISSION_DENIED, alljoyn_securityapplicationproxy_updateidentity(managedAppSecurityApplicationProxy,
                                                                                    managedAppIdentityCertificateChain,
                                                                                    (AJ_PCSTR*)managedAppSignedManifests,
                                                                                    1U));
}

TEST_F(SecurityApplicationProxyFullSetupTest, shouldPassStartManagementCallForValidProxyAndInstalledMembership)
{
    EXPECT_EQ(ER_OK, alljoyn_securityapplicationproxy_startmanagement(managedAppSecurityApplicationProxy));
}

TEST_F(SecurityApplicationProxyFullSetupTest, shouldReturnErrorWhenCallingStartManagementTwice)
{
    ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_startmanagement(managedAppSecurityApplicationProxy));

    EXPECT_EQ(ER_MANAGEMENT_ALREADY_STARTED, alljoyn_securityapplicationproxy_startmanagement(managedAppSecurityApplicationProxy));
}

TEST_F(SecurityApplicationProxyFullSetupTest, shouldCallStartManagementCallbackAfterStartManagement)
{
    startManagementHappened = false;
    ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_startmanagement(managedAppSecurityApplicationProxy));

    EXPECT_TRUE(waitForTrueOrTimeout(&startManagementHappened));
}

TEST_F(SecurityApplicationProxyFullSetupTest, shouldReturnErrorWhenCallingEndManagementBeforeStart)
{
    EXPECT_EQ(ER_MANAGEMENT_NOT_STARTED, alljoyn_securityapplicationproxy_endmanagement(managedAppSecurityApplicationProxy));
}

TEST_F(SecurityApplicationProxyFullSetupTest, shouldPassEndManagementCallForValidProxyAndInstalledMembership)
{
    ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_startmanagement(managedAppSecurityApplicationProxy));

    EXPECT_EQ(ER_OK, alljoyn_securityapplicationproxy_endmanagement(managedAppSecurityApplicationProxy));
}

TEST_F(SecurityApplicationProxyFullSetupTest, shouldCallEndManagementCallbackAfterEndManagement)
{
    endManagementHappened = false;
    ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_startmanagement(managedAppSecurityApplicationProxy));
    ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_endmanagement(managedAppSecurityApplicationProxy));

    EXPECT_TRUE(waitForTrueOrTimeout(&endManagementHappened));
}

TEST_F(SecurityApplicationProxyFullSetupTest, shouldPassStartManagementAfterEnd)
{
    ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_startmanagement(managedAppSecurityApplicationProxy));
    ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_endmanagement(managedAppSecurityApplicationProxy));

    EXPECT_EQ(ER_OK, alljoyn_securityapplicationproxy_startmanagement(managedAppSecurityApplicationProxy));
}

TEST_F(SecurityApplicationProxyFullSetupTest, shouldPassUpdatePolicyForValidProxyAndInstalledMembership)
{
    EXPECT_EQ(ER_OK, alljoyn_securityapplicationproxy_updatepolicy(managedAppSecurityApplicationProxy, VALID_NEWER_POLICY));
}

TEST_F(SecurityApplicationProxyFullSetupTest, shouldCallPolicyChangedCallbackAfterPolicyUpdate)
{
    policyChangeHappened = false;
    ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_updatepolicy(managedAppSecurityApplicationProxy, VALID_NEWER_POLICY));

    EXPECT_TRUE(waitForTrueOrTimeout(&policyChangeHappened));
}

TEST_F(SecurityApplicationProxyFullSetupTest, shouldReturnErrorWhenUpdatingPolicyWithNotNewerNumber)
{
    ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_updatepolicy(managedAppSecurityApplicationProxy, newPolicy));
    ASSERT_EQ(ER_OK, alljoyn_proxybusobject_secureconnection((alljoyn_proxybusobject)managedAppSecurityApplicationProxy, true));

    EXPECT_EQ(ER_POLICY_NOT_NEWER, alljoyn_securityapplicationproxy_updatepolicy(managedAppSecurityApplicationProxy, oldPolicy));
}

TEST_F(SecurityApplicationProxyFullSetupTest, shouldPassWhenUpdatingPolicyWithNewerNumber)
{
    ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_updatepolicy(managedAppSecurityApplicationProxy, oldPolicy));
    ASSERT_EQ(ER_OK, alljoyn_proxybusobject_secureconnection((alljoyn_proxybusobject)managedAppSecurityApplicationProxy, true));

    EXPECT_EQ(ER_OK, alljoyn_securityapplicationproxy_updatepolicy(managedAppSecurityApplicationProxy, newPolicy));
}

TEST_F(SecurityApplicationProxyFullSetupTest, shouldPassUpdateIdentityForValidProxyAndInstalledMembership)
{
    EXPECT_EQ(ER_OK, alljoyn_securityapplicationproxy_updateidentity(managedAppSecurityApplicationProxy,
                                                                     managedAppIdentityCertificate,
                                                                     (AJ_PCSTR*)managedAppSignedManifests,
                                                                     1U));
}

TEST_F(SecurityApplicationProxyFullSetupTest, shouldNotCallPolicyChangedCallbackAfterIdentityCertUpdate)
{
    modifyManagedAppIdentityCertAndManifests();
    policyChangeHappened = false;
    ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_updateidentity(managedAppSecurityApplicationProxy,
                                                                     managedAppIdentityCertificate,
                                                                     (AJ_PCSTR*)managedAppSignedManifests,
                                                                     1U));

    EXPECT_FALSE(waitForTrueOrTimeout(&policyChangeHappened));
}

TEST_F(SecurityApplicationProxyFullSetupTest, shouldPassResetForValidProxyAndInstalledMembership)
{
    EXPECT_EQ(ER_OK, alljoyn_securityapplicationproxy_reset(managedAppSecurityApplicationProxy));
}

TEST_F(SecurityApplicationProxyFullSetupTest, shouldCallResetCallbackAfterReset)
{
    factoryResetHappened = false;
    ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_reset(managedAppSecurityApplicationProxy));

    EXPECT_TRUE(waitForTrueOrTimeout(&factoryResetHappened));
}

TEST_F(SecurityApplicationProxyFullSetupTest, shouldResetAppToClaimableStateAfterReset)
{
    factoryResetHappened = false;
    ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_reset(managedAppSecurityApplicationProxy));
    ASSERT_TRUE(waitForTrueOrTimeout(&factoryResetHappened));
    ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_getapplicationstate(managedAppSecurityApplicationProxy, &retrievedManagedAppApplicationState));

    EXPECT_EQ(CLAIMABLE, retrievedManagedAppApplicationState);
}

TEST_F(SecurityApplicationProxyFullSetupTest, shouldPassResetPolicyForValidProxyAndInstalledMembership)
{
    EXPECT_EQ(ER_OK, alljoyn_securityapplicationproxy_resetpolicy(managedAppSecurityApplicationProxy));
}

TEST_F(SecurityApplicationProxyFullSetupTest, shouldCallResetPolicyCallbackAfterReset)
{
    policyChangeHappened = false;
    ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_resetpolicy(managedAppSecurityApplicationProxy));

    EXPECT_TRUE(waitForTrueOrTimeout(&policyChangeHappened));
}

TEST_F(SecurityApplicationProxyFullSetupTest, shouldNotResetAppToClaimableStateAfterResetPolicy)
{
    policyChangeHappened = false;
    ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_resetpolicy(managedAppSecurityApplicationProxy));
    ASSERT_TRUE(waitForTrueOrTimeout(&policyChangeHappened));
    ASSERT_EQ(ER_OK, alljoyn_securityapplicationproxy_getapplicationstate(managedAppSecurityApplicationProxy, &retrievedManagedAppApplicationState));

    EXPECT_EQ(CLAIMED, retrievedManagedAppApplicationState);
}