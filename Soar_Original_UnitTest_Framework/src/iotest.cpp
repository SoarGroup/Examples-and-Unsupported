#include "portability.h"

#include "unittest.h"

#include <string>
#include <vector>
#include <sstream>
#include <bitset>

#include "sml_Connection.h"
#include "sml_Client.h"
#include "sml_Utils.h"
#include "thread_Event.h"
#include "sml_Names.h"
#include "soar_instance.h"

enum eKernelOptions
{
    kOpt_NONE,
    kOpt_EMBEDDED,
    kOpt_USE_CLIENT_THREAD,
    kOpt_FULLY_OPTIMIZED,
    kOpt_AUTO_COMMIT_ENABLED,
    NUM_KERNEL_OPTIONS,
};

typedef std::bitset<NUM_KERNEL_OPTIONS> KernelBitset;

class IOTest : public CPPUNIT_NS::TestCase
{
        CPPUNIT_TEST_SUITE(IOTest);      // The name of this class
#ifndef SKIP_SLOW_TESTS
        #ifdef DO_IO_TESTS
        CPPUNIT_TEST(testInputLeak);
        CPPUNIT_TEST(testInputLeak2);
        CPPUNIT_TEST(testInputLeak3);
        /* -- This tests fails and seems to have for quite some time. (11-23-2013) -- */
        //CPPUNIT_TEST( testInputLeak4 );
        CPPUNIT_TEST(testOutputLeak1);   // bug 1062
#endif
#endif
        CPPUNIT_TEST_SUITE_END();

    public:
        void setUp();       // Called before each function outlined by CPPUNIT_TEST
        void tearDown();    // Called after each function outlined by CPPUNIT_TEST

    protected:
        void testInputLeak();  // only string
        void testInputLeak2(); // explicitly delete both
        void testInputLeak3(); // only delete identifier
        void testInputLeak4(); // do something with shared ids
        void testOutputLeak1(); // output input wme created but not destroyed

        void createKernelAndAgents(const KernelBitset& options, int port = 12121);

        sml::Kernel* pKernel;
        bool remote;

        bool alreadyDestroyed;
};

CPPUNIT_TEST_SUITE_REGISTRATION(IOTest);   // Registers the test so it will be used

void IOTest::setUp()
{
    pKernel = NULL;
    remote = false;
    alreadyDestroyed = false;

    // kernel initialized in test
}

void IOTest::tearDown()
{
    if (alreadyDestroyed)
    {
        return;
    }

    if (!pKernel)
    {
        return;
    }

    // Agent deletion
    sml::Agent* pAgent = pKernel->GetAgent("IOTest") ;
    CPPUNIT_ASSERT(pAgent != NULL);

    CPPUNIT_ASSERT(pKernel->DestroyAgent(pAgent));

    pKernel->Shutdown() ;

    // Delete the kernel.  If this is an embedded connection this destroys the kernel.
    // If it's a remote connection we just disconnect.
    delete pKernel ;
    pKernel = NULL;

    //if ( remote )
    //{
    //  cleanUpListener();
    //  if ( verbose ) std::cout << "Cleaned up listener." << std::endl;
    //}
}

void IOTest::createKernelAndAgents(const KernelBitset& options, int port)
{
    CPPUNIT_ASSERT(pKernel == NULL);

    if (options.test(kOpt_EMBEDDED))
    {
        CPPUNIT_ASSERT(!remote);
        if (options.test(kOpt_USE_CLIENT_THREAD))
        {
            pKernel = sml::Kernel::CreateKernelInCurrentThread(options.test(kOpt_FULLY_OPTIMIZED), sml::Kernel::GetDefaultPort());
        }
        else
        {
            pKernel = sml::Kernel::CreateKernelInNewThread(sml::Kernel::GetDefaultPort());
        }
    }
    else
    {
        CPPUNIT_ASSERT(remote);
        pKernel = sml::Kernel::CreateRemoteConnection(true, 0, port);
    }

    CPPUNIT_ASSERT(pKernel != NULL);
    CPPUNIT_ASSERT_MESSAGE(pKernel->GetLastErrorDescription(), !pKernel->HadError());

    /* Sets Soar's output settings to what the unit tests expect.  Prevents
     * debug trace code from being output and causing some tests to appear to fail. */
    #ifdef CONFIGURE_SOAR_FOR_UNIT_TESTS
    configure_for_unit_tests();
    #endif

    pKernel->SetAutoCommit(options.test(kOpt_AUTO_COMMIT_ENABLED)) ;

    // Set this to true to give us lots of extra debug information on remote clients
    // (useful in a test app like this).
    // pKernel->SetTraceCommunications(true) ;

    CPPUNIT_ASSERT(std::string(pKernel->GetSoarKernelVersion()) == std::string(sml::sml_Names::kSoarVersionValue));

    // Report the number of agents (always 0 unless this is a remote connection to a CLI or some such)
    CPPUNIT_ASSERT(pKernel->GetNumberAgents() == 0);

    // NOTE: We don't delete the agent pointer.  It's owned by the kernel
    sml::Agent* pAgent = pKernel->CreateAgent("IOTest") ;
    CPPUNIT_ASSERT_MESSAGE(pKernel->GetLastErrorDescription(), !pKernel->HadError());
    CPPUNIT_ASSERT(pAgent != NULL);

    // a number of tests below depend on running full decision cycles.
    pAgent->ExecuteCommandLine("soar stop-phase input") ;
    CPPUNIT_ASSERT_MESSAGE("soar stop-phase input", pAgent->GetLastCommandLineResult());

    pAgent->ExecuteCommandLine("watch 0") ;
    pAgent->ExecuteCommandLine("waitsnc --on") ;

    CPPUNIT_ASSERT(pKernel->GetNumberAgents() == 1);
}

void IOTest::testInputLeak()
{
    KernelBitset options(0);
    options.set(kOpt_EMBEDDED);
    options.set(kOpt_USE_CLIENT_THREAD);
    options.set(kOpt_FULLY_OPTIMIZED);
    options.set(kOpt_AUTO_COMMIT_ENABLED);
    createKernelAndAgents(options);

    sml::Agent* pAgent = pKernel->GetAgent("IOTest") ;
    CPPUNIT_ASSERT(pAgent != 0);

    sml::Identifier* pInputLink = pAgent->GetInputLink();
    sml::StringElement* pFooBar = 0;

    //_CrtMemState memState;

    //_CrtMemCheckpoint( &memState );
    //_CrtSetBreakAlloc( 2406 );
    for (int count = 0; count < 50000; ++count)
    {
        if (count % 2 == 0)
        {
            // even case

            // creating the wme
            CPPUNIT_ASSERT(pFooBar == 0);

            pFooBar = pInputLink->CreateStringWME("foo", "bar");
            CPPUNIT_ASSERT(pFooBar != 0);

            pKernel->RunAllAgents(1);
        }
        else
        {
            // odd case
            // deleting the wme
            CPPUNIT_ASSERT(pFooBar != 0);
            pFooBar->DestroyWME();

            pFooBar = 0;

            pKernel->RunAllAgents(1);

            //_CrtMemDumpAllObjectsSince( &memState );
        }
    }
}

void IOTest::testInputLeak2()
{
    KernelBitset options(0);
    options.set(kOpt_EMBEDDED);
    options.set(kOpt_USE_CLIENT_THREAD);
    options.set(kOpt_FULLY_OPTIMIZED);
    options.set(kOpt_AUTO_COMMIT_ENABLED);
    createKernelAndAgents(options);

    sml::Agent* pAgent = pKernel->GetAgent("IOTest") ;
    CPPUNIT_ASSERT(pAgent != 0);

    sml::Identifier* pInputLink = pAgent->GetInputLink();
    sml::Identifier* pIdentifier = 0;
    sml::StringElement* pFooBar = 0;

    //_CrtMemState memState;

    //_CrtMemCheckpoint( &memState );
    //_CrtSetBreakAlloc( 4951 );
    for (int count = 0; count < 50000; ++count)
    {
        if (count % 2 == 0)
        {
            // even case

            // creating the wme
            CPPUNIT_ASSERT(pIdentifier == 0);
            CPPUNIT_ASSERT(pFooBar == 0);

            pIdentifier = pInputLink->CreateIdWME("alpha");
            CPPUNIT_ASSERT(pIdentifier != 0);

            pFooBar = pIdentifier->CreateStringWME("foo", "bar");
            CPPUNIT_ASSERT(pFooBar != 0);

            pKernel->RunAllAgents(1);
        }
        else
        {
            // odd case
            // deleting the wme
            CPPUNIT_ASSERT(pFooBar != 0);
            pFooBar->DestroyWME();

            CPPUNIT_ASSERT(pIdentifier != 0);
            pIdentifier->DestroyWME();

            pIdentifier = 0;
            pFooBar = 0;

            pKernel->RunAllAgents(1);

            //_CrtMemDumpAllObjectsSince( &memState );
        }
    }
}

void IOTest::testInputLeak3()
{
    KernelBitset options(0);
    options.set(kOpt_EMBEDDED);
    options.set(kOpt_USE_CLIENT_THREAD);
    options.set(kOpt_FULLY_OPTIMIZED);
    options.set(kOpt_AUTO_COMMIT_ENABLED);
    createKernelAndAgents(options);

    sml::Agent* pAgent = pKernel->GetAgent("IOTest") ;
    CPPUNIT_ASSERT(pAgent != 0);

    sml::Identifier* pInputLink = pAgent->GetInputLink();
    sml::Identifier* pIdentifier = 0;
    sml::StringElement* pFooBar = 0;

    //_CrtMemState memState;

    //_CrtMemCheckpoint( &memState );
    //_CrtSetBreakAlloc( 2451 );
    for (int count = 0; count < 50000; ++count)
    {
        if (count % 2 == 0)
        {
            // even case

            // creating the wme
            CPPUNIT_ASSERT(pIdentifier == 0);
            CPPUNIT_ASSERT(pFooBar == 0);

            pIdentifier = pInputLink->CreateIdWME("alpha");
            CPPUNIT_ASSERT(pIdentifier != 0);

            pFooBar = pIdentifier->CreateStringWME("foo", "bar");
            CPPUNIT_ASSERT(pFooBar != 0);

            pKernel->RunAllAgents(1);
        }
        else
        {
            // odd case
            // deleting the wme
            CPPUNIT_ASSERT(pIdentifier != 0);
            pIdentifier->DestroyWME();

            pIdentifier = 0;
            pFooBar = 0;

            pKernel->RunAllAgents(1);

            //_CrtMemDumpAllObjectsSince( &memState );
        }
    }
}

void IOTest::testInputLeak4()
{
    KernelBitset options(0);
    options.set(kOpt_EMBEDDED);
    options.set(kOpt_USE_CLIENT_THREAD);
    options.set(kOpt_FULLY_OPTIMIZED);
    options.set(kOpt_AUTO_COMMIT_ENABLED);
    createKernelAndAgents(options);

    sml::Agent* pAgent = pKernel->GetAgent("IOTest") ;
    CPPUNIT_ASSERT(pAgent != 0);

    sml::Identifier* pInputLink = pAgent->GetInputLink();
    sml::Identifier* pIdentifier = 0;
    sml::StringElement* pFooBar = 0;
    sml::Identifier* pSharedIdentifier = 0;

    //_CrtMemState memState;

    //_CrtMemCheckpoint( &memState );
    //_CrtSetBreakAlloc( 2451 );
    for (int count = 0; count < 50000; ++count)
    {
        if (count % 2 == 0)
        {
            // even case

            // creating the wme
            CPPUNIT_ASSERT(pIdentifier == 0);
            CPPUNIT_ASSERT(pFooBar == 0);
            CPPUNIT_ASSERT(pSharedIdentifier == 0);

            pIdentifier = pInputLink->CreateIdWME("alpha");
            CPPUNIT_ASSERT(pIdentifier != 0);

            pFooBar = pIdentifier->CreateStringWME("foo", "bar");
            CPPUNIT_ASSERT(pFooBar != 0);

            pSharedIdentifier = pInputLink->CreateSharedIdWME("alpha", pIdentifier);
            CPPUNIT_ASSERT(pSharedIdentifier != 0);

            pKernel->RunAllAgents(1);
        }
        else
        {
            // odd case
            // deleting the wme
            CPPUNIT_ASSERT(pIdentifier != 0);
            pIdentifier->DestroyWME();

            CPPUNIT_ASSERT(pSharedIdentifier != 0);
            pSharedIdentifier->DestroyWME();

            pIdentifier = 0;
            pFooBar = 0;
            pSharedIdentifier = 0;

            pKernel->RunAllAgents(1);

            //_CrtMemDumpAllObjectsSince( &memState );
        }
    }
}

void IOTest::testOutputLeak1()
{
    KernelBitset options(0);
    options.set(kOpt_EMBEDDED);
    options.set(kOpt_USE_CLIENT_THREAD);
    options.set(kOpt_FULLY_OPTIMIZED);
    options.set(kOpt_AUTO_COMMIT_ENABLED);
    createKernelAndAgents(options);

    sml::Agent* pAgent = pKernel->GetAgent("IOTest") ;
    CPPUNIT_ASSERT(pAgent != 0);

    pAgent->LoadProductions("test_agents/testoutputleak.soar") ;
    CPPUNIT_ASSERT_MESSAGE("loadProductions", pAgent->GetLastCommandLineResult());

    /*sml::Identifier* pOutputLink = */pAgent->GetOutputLink();

    pKernel->RunAllAgents(1);

#ifdef _WIN32
#ifdef _DEBUG
    _CrtMemState memState;

    _CrtMemCheckpoint(&memState);
    //_CrtSetBreakAlloc( 3020 );
#endif
#endif

    CPPUNIT_ASSERT(pAgent->GetNumberCommands() == 1);
    sml::Identifier* pCommand = pAgent->GetCommand(0) ;
    pCommand->AddStatusComplete();

    // need to pass input phase
    pKernel->RunAllAgents(2);

    CPPUNIT_ASSERT(pKernel->DestroyAgent(pAgent));
    pKernel->Shutdown() ;
    delete pKernel ;
    pKernel = 0;
    alreadyDestroyed = true;

#ifdef _WIN32
#ifdef _DEBUG
    _CrtMemDumpAllObjectsSince(&memState);
#endif
#endif

}
