#include "Routers.hpp"
#define BOOST_TEST_MODULE RouterTests
#include <boost/test/included/unit_test.hpp>


using namespace boost::asio;
using namespace Tests;
awaitable<void> test()
{
    auto executor = co_await this_coro::executor;
    auto const address1 = ip::make_address("127.0.0.2");
    auto const address2 = ip::make_address("127.0.0.3");
    auto const router1 = Router::create<Router::Symmetric>(make_strand(executor), address1);
    auto const router2 = Router::create<Router::FullCone>(make_strand(executor), address2);

    auto userA = router1->createUser();
    auto userB = router1->createUser();

    auto userC = router2->createUser();
    auto userD = router2->createUser();

    co_await userA.send(Router::EndPoint{ address1, static_cast<unsigned short>(userC.getId()) }, "test");
    auto [from, data] = co_await userC.receive();
}

BOOST_AUTO_TEST_CASE(Test)
{
    
    auto context = io_context{};
    co_spawn(context, test, detached);
    context.run();
}