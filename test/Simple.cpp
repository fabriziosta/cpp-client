#include "infinispan/hotrod/ConfigurationBuilder.h"
#include "infinispan/hotrod/RemoteCacheManager.h"
#include "infinispan/hotrod/RemoteCache.h"
#include "infinispan/hotrod/Version.h"

#include "infinispan/hotrod/JBasicMarshaller.h"
#include "infinispan/hotrod/JBossMarshaller.h"
#include <stdlib.h>
#include <iostream>
#include <memory>
#include <typeinfo>
#include <thread>
#include <future>
#include <chrono>

// For CTest: return 0 if all tests pass, non-zero otherwise.

using namespace infinispan::hotrod;

namespace infinispan {
namespace hotrod {
namespace transport {

class MyRoundRobinBalancingStrategy: public FailOverRequestBalancingStrategy {
public:
    MyRoundRobinBalancingStrategy() :
            index(0) {
    }

    static FailOverRequestBalancingStrategy *newInstance() {
        return new MyRoundRobinBalancingStrategy();
    }

    void setServers(const std::vector<transport::InetSocketAddress> &s) {
        servers = s;
        // keep the old index if possible so that we don't produce more requests for the first server
        if (index >= servers.size()) {
            index = 0;
        }
    }

    ~MyRoundRobinBalancingStrategy() {
    }

    const transport::InetSocketAddress &getServerByIndex(size_t pos) {
        const transport::InetSocketAddress &server = servers[pos];
        return server;
    }
private:
    std::vector<transport::InetSocketAddress> servers;
    size_t index;
    const transport::InetSocketAddress &nextServer(
            const std::set<transport::InetSocketAddress>& failedServers) {
        for (unsigned int i = 0; i <= servers.size(); i++) {
            const transport::InetSocketAddress &server = getServerByIndex(
                    index++);
            if (failedServers.empty() || failedServers.find(server) != failedServers.end()
                    || i > failedServers.size()) {
                if (index >= servers.size()) {
                    index = 0;
                }
            }
            return server;
        }

        throw Exception("Bad news, no server available.");
    }
};

}
}
}

template<class T>
void assert_not_null(const std::string& message, int line, const std::unique_ptr<T>& pointer) {
    if (pointer.get() == 0) {
        std::cerr << message << ":" << line << std::endl;
        std::cerr.flush();
        throw std::exception();
    }
}

class StaticBasicMarshaller {
public:
    template<typename R> static R unmarshall(const std::vector<char>& b);
    template<typename R> static std::vector<char> marshall(const R& s);
};

template<> std::string* StaticBasicMarshaller::unmarshall(const std::vector<char>& b) {
    std::string* s = new std::string(b.begin(), b.end());
    return s;
}

template<> int* StaticBasicMarshaller::unmarshall(const std::vector<char>& b) {
    int* s = new int();
    std::memcpy(s, b.data(), sizeof(*s));
    return s;
}

template<> std::vector<char> StaticBasicMarshaller::marshall(const std::string& s) {
    return std::vector<char>(s.begin(), s.end());
}


template<> std::vector<char> StaticBasicMarshaller::marshall(const int& s) {
    return std::vector<char>(&s, (&s)+1);
}


template<typename K, typename V>
int basicTest(RemoteCacheManager &cacheManager, RemoteCache<K, V> &cache) {

    std::cout << "HotRod C++ Library version " << cache.getVersion() << std::endl;
    std::cout << "Protocol " << cache.getProtocolVersion() << std::endl;

    std::string k1("key13");
    std::string k2("key14");
    std::string v1("boron");
    std::string v2("chlorine");

    cache.clear();

    // put
    cache.put(k1, v1);
    std::unique_ptr<std::string> rv(cache.get(k1));
    assert_not_null("get returned null!", __LINE__, rv);
    if (rv->compare(v1)) {
        std::cerr << "get/put fail for " << k1 << " got " << *rv << " expected " << v1 << std::endl;
        return 1;
    }

    cache.put(k2, v2);
    std::unique_ptr<std::string> rv2(cache.get(k2));
    assert_not_null("get returned null!", __LINE__, rv2);
    if (rv2->compare(v2)) {
        std::cerr << "get/put fail for " << k2 << " got " << *rv2 << " expected " << v2 << std::endl;
        return 1;
    }

    std::unique_ptr<std::string> badValue(cache.get(std::string("no such key in the cache")));
    if (badValue.get()) {
        std::cout << "non-existent key failure, got " << *badValue << std::endl;
        return 1;
    }
    std::cout << "PASS: simple get/put" << std::endl;

    std::set<std::string> getAllKeySet { "key13", "key14" };
    std::map<std::shared_ptr<std::string>, std::shared_ptr<std::string> > getAllResult = cache.getAll(getAllKeySet);
    for (const auto& item : getAllResult)
    {
        std::unique_ptr<std::string> rvItem(cache.get(*item.first));
        std::cout << "    key: " << *item.first << ", getAll value: " << *item.second << ", get value: " << *rvItem
                << std::endl;
        if (rvItem->compare(*item.second))
                {
            std::cerr << "getAll fail for " << *item.first << " got " << *item.second << " expected " << *rvItem
                    << std::endl;
        }

    }
    std::cout << "PASS: getAll" << std::endl;

    {
        std::string k3("rolling");
        std::string v3("stones");
        std::string v4("beatles");

        // putIfAbsent
        cache.putIfAbsent(k3, v3);
        std::unique_ptr<std::string> rv3(cache.get(k3));
        assert_not_null("get returned null!", __LINE__, rv3);
        if (rv3->compare(v3)) {
            std::cerr << "putIfAbsent fail for " << k3 << " got " << *rv3 << " expected " << v3 << std::endl;
            return 1;
        }

        cache.putIfAbsent(k3, v4);
        std::unique_ptr<std::string> rv4(cache.get(k3));
        assert_not_null("get returned null!", __LINE__, rv4);
        if (rv4->compare(v3)) {
            std::cerr << "putIfAbsent fail for " << k3 << " got " << *rv4 << " expected " << v3 << std::endl;
            return 1;
        }

        std::cout << "PASS: simple putIfAbsent" << std::endl;
    }
    {
        std::string k3("rolling");
        std::string v3("stones");
        std::string v4("beatles");

        // putIfAbsent
        cache.putIfAbsent(k3, v3);
        std::unique_ptr<std::string> rv3(cache.get(k3));
        assert_not_null("get returned null!", __LINE__, rv3);
        if (rv3->compare(v3)) {
            std::cerr << "putIfAbsent fail for " << k3 << " got " << *rv3 << " expected " << v3 << std::endl;
            return 1;
        }

        std::unique_ptr<std::string> rv4(cache.withFlags(FORCE_RETURN_VALUE).putIfAbsent(k3, v4));
        assert_not_null("get returned null!", __LINE__, rv4);
        if (rv4->compare(v3)) {
            std::cerr << "putIfAbsent fail for " << k3 << " got " << *rv4 << " expected " << v3 << std::endl;
            return 1;
        }

        std::cout << "PASS: simple putIfAbsent with force return value" << std::endl;
    }

    std::string k3("rolling");
    std::string v3("stones");
    std::string v4("beatles");

    cache.put(k3, v3, 10, SECONDS);
    // getWithMetadata
    std::pair<std::shared_ptr<std::string>, MetadataValue> rv5 = cache.getWithMetadata(k3);
    if (!rv5.first.get() || rv5.second.lifespan != 10) {
        std::cerr << "getWithMetadata with mortal entry fail for " << k3 << " not found" << std::endl;
        return 1;
    }
    std::cout << "PASS: simple getWithMetadata with mortal entry" << std::endl;

    cache.put(k3, v3);
    // getWithMetadata
    rv5 = cache.getWithMetadata(k3);
    if (!rv5.first.get()
            || rv5.second.lifespan >= 0 || rv5.second.created >= 0
            || rv5.second.maxIdle >= 0 || rv5.second.lastUsed >= 0) {
        std::cerr << "getWithMetadata with immortal entry fail for " << k3 << std::endl;
        return 1;
    }
    std::cout << "PASS: simple getWithMetadata with immortal entry" << std::endl;

    // getWithVersion
    std::pair<std::shared_ptr<std::string>, VersionedValue> rv5a = cache.getWithVersion(k3);
    if (!rv5a.first.get()) {
        std::cerr << "getWithVersion fail for " << k3 << " not found" << std::endl;
        return 1;
    }

    std::cout << "PASS: simple getWithVersion" << std::endl;

    // replaceWithVersion
    cache.replaceWithVersion(k3, v4, rv5.second.version);
    std::unique_ptr<std::string> rv6(cache.get(k3));
    assert_not_null("get returned null!", __LINE__, rv6);
    if (rv6->compare(v4)) {
        std::cerr << "replaceWithVersion fail for " << k3 << " got " << *rv6 << " expected " << v4 << std::endl;
        return 1;
    }

    cache.replaceWithVersion(k3, v3, rv5.second.version);
    std::unique_ptr<std::string> rv7(cache.get(k3));
    assert_not_null("get returned null!", __LINE__, rv7);
    if (rv7->compare(v4)) {
        std::cerr << "replaceWithVersion fail for " << k3 << " got " << *rv7 << " expected " << v4 << std::endl;
        return 1;
    }

    std::cout << "PASS: simple replaceWithVersion" << std::endl;

    // size
    uint64_t size = cache.size();
    if (size != 3) {
        std::cerr << "size fail got " << size << " expected 3 " << std::endl;
        return 1;
    }

    std::cout << "PASS: simple size" << std::endl;

    // stats
    const std::map<std::string, std::string>& stats = cache.stats();
    if (stats.empty()) {
        std::cerr << "stats fail with empty map" << std::endl;
        return 1;
    }

    std::cout << "  stats result is:" << std::endl;
    for (std::map<std::string, std::string>::const_iterator i = stats.begin(); i != stats.end(); i++) {
        std::cout << "    key: " << i->first << ", value: " << i->second << std::endl;
    }

    std::cout << "PASS: simple stats" << std::endl;

    // clear
    cache.clear();
    uint64_t size2 = cache.size();
    if (size2 != 0) {
        std::cerr << "clear fail cache has size " << size2 << " expected 0 " << std::endl;
        return 1;
    }

    std::cout << "PASS: simple clear" << std::endl;

    std::string k4("real");
    std::string v5("madrid");
    std::string v6("barcelona");

    // put with FORCE_RETURN_VALUE flag
    std::unique_ptr<std::string> rv8(cache.withFlags(FORCE_RETURN_VALUE).put(k4, v5));
    if (rv8.get()) {
        std::cerr << "put with FORCE_RETURN_FLAG fail for " << k4 << " got " << *rv8 << " expected null pointer"
                << std::endl;
        return 1;
    }

    std::unique_ptr<std::string> rv9(cache.withFlags(FORCE_RETURN_VALUE).put(k4, v6));
    assert_not_null("get returned null!", __LINE__, rv9);
    if (rv9->compare(v5)) {
        std::cerr << "put with FORCE_RETURN_FLAG fail for " << k4 << " got " << *rv9 << " expected " << v5 << std::endl;
        return 1;
    }

    std::cout << "PASS: simple put with FORCE_RETURN_FLAG" << std::endl;

    // keySet
    std::set<std::shared_ptr<std::string> > keySet = cache.keySet();
    if (keySet.size() != 1) {
        std::cerr << "keySet fail got " << keySet.size() << " entries expected 1" << std::endl;
        return 1;
    }

    std::cout << "  keySet result is:" << std::endl;
    for (std::set<std::shared_ptr<std::string> >::const_iterator i = keySet.begin(); i != keySet.end(); i++) {
        std::cout << "    key: " << *i->get() << std::endl;
    }

    std::cout << "PASS: simple keySet" << std::endl;

    // getBulk
    std::map<std::shared_ptr<std::string>, std::shared_ptr<std::string> > map = cache.getBulk();
    if (map.size() != 1) {
        std::cerr << "getBulk fail got" << map.size() << " entries expected 1" << std::endl;
        return 1;
    }

    std::cout << "  getBulk result is:" << std::endl;
    for (std::map<std::shared_ptr<std::string>, std::shared_ptr<std::string> >::const_iterator i = map.begin();
            i != map.end(); i++) {
        std::cout << "    key: " << *i->first.get() << ", value: " << *i->second.get() << std::endl;
    }

    std::cout << "PASS: simple getBulk" << std::endl;

    // replace
    cache.replace(k4, v5);
    std::unique_ptr<std::string> rv10(cache.get(k4));
    assert_not_null("get returned null!", __LINE__, rv10);
    if (rv10->compare(v5)) {
        std::cerr << "replace fail for " << k4 << " got " << *rv10 << " expected " << v5 << std::endl;
        return 1;
    }

    std::cout << "PASS: simple replace" << std::endl;
    try {
        RemoteCache<std::string, std::string> namedCache =
                cacheManager.getCache<std::string, std::string>("namedCache", false);
        std::unique_ptr<std::string> namedCacheV1(namedCache.get("k1"));
    }
    catch (...)
    {
        return 1;
    }
    std::cout << "PASS: get for namedCache" << std::endl;

    // get non-existing cache
    try {
        RemoteCache<std::string, std::string> non_existing =
                cacheManager.getCache<std::string, std::string>("non-existing", false);
        std::cerr << "fail getCache for non-existing cache didn't throw exception" << std::endl;
        return 1;
    } catch (const HotRodClientException &ex) {
        std::cout << "PASS: get non-existing cache" << std::endl;
    } catch (const Exception &e) {
        std::cout << "is: " << typeid(e).name() << '\n';
        std::cerr << "fail unexpected exception: " << e.what() << std::endl;
        return 1;
    }

    // Async test
    std::string ak1("asyncK1");
    std::string av1("sun");

    std::string ak2("asyncK2");
    std::string av2("mercury");

    cache.clear();

    bool flag = false;
    // Now user pass a lambda func that will be executed in the async thread after the put completion
    std::future<std::string*> future_put1 = cache.putAsync(ak2, av2, 0, 0, [&] (std::string *v) {flag=true; return v;});
    {
        // If the async put is not completed flag must be false
        if (future_put1.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                {
            if (flag)
            {
                std::cerr << "fail: expected false got true" << std::endl;
                return 1;
            }
        }
    }
    // Now wait for put completion
    future_put1.wait();
    {
        // The user lambda must be executed so flag must be true
        if (!flag)
        {
            std::cerr << "fail: expected true got false" << std::endl;
            return 1;
        }
    }
    // Same test for get
    flag = false;
    std::future<std::string*> future_get1 = cache.getAsync(ak2, [&] (std::string *v) {flag=true; return v;});

    if (future_get1.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            {
        if (flag)
        {
            std::cerr << "fail: expected false got true" << std::endl;
            return 1;
        }
    }
    future_get1.wait();
    {
        if (!flag)
        {
            std::cerr << "fail: expected true got false" << std::endl;
            return 1;
        }
    }
    std::string* arv3 = future_get1.get();

    if (!arv3 || arv3->compare(av2))
            {
        std::cerr << "fail: expected " << av2 << " got " << (arv3 ? *arv3 : "null") << std::endl;
        return 1;
    }
    delete arv3;
    std::cout << "PASS: Async test" << std::endl;
    return 0;
}




int main(int argc, char** argv) {

    int result = 0;
    std::cout << "Tests for CacheManager" << std::endl;
    {
        ConfigurationBuilder builder;
        builder.protocolVersion(argc > 1 ? argv[1] : Configuration::PROTOCOL_VERSION_28);
        builder.addServer().host(argc > 2 ? argv[2] : "127.0.0.1").port(argc > 3 ? atoi(argv[3]) : 11222);
//        builder.balancingStrategyProducer(transport::MyRoundRobinBalancingStrategy::newInstance);
        builder.balancingStrategyProducer(nullptr);
        RemoteCacheManager cacheManager(builder.build(), false);
        RemoteCache<std::string, std::string> &cachef1 = cacheManager.getCache<std::string, std::string>("default",false);
        RemoteCache<std::string, std::string> &cachet1 = cacheManager.getCache<std::string, std::string>("default",true);
        RemoteCache<std::string, std::string> &cachenf1 = cacheManager.getCache<std::string, std::string>("namedCache",
                false);
        RemoteCache<std::string, std::string> &cachent1 = cacheManager.getCache<std::string, std::string>("namedCache",
                true);

        RemoteCache<std::string, std::string> &cachet2 = cacheManager.getCache<std::string, std::string>("default",true);
        RemoteCache<std::string, std::string> &cachef2 = cacheManager.getCache<std::string, std::string>("default",false);
        RemoteCache<std::string, std::string> &cachenf2 = cacheManager.getCache<std::string, std::string>("namedCache",
                false);
        RemoteCache<std::string, std::string> &cachent2 = cacheManager.getCache<std::string, std::string>("namedCache",
                true);
        if (&cachef1 != &cachef2) {
            std::cerr << "Got two different cache instances(false): " << &cachef1 << "   " << &cachef2 << std::endl;
            result = 1;
        }
        if (&cachet1 != &cachet2) {
            std::cerr << "Got two different cache instances(false): " << &cachet1 << "   " << &cachet2 << std::endl;
            result = 1;
        }
        if (&cachef1 != &cachef2) {
            std::cerr << "Got two different cache instances(false): " << &cachenf1 << "   " << &cachenf2 << std::endl;
            result = 1;
        }
        if (&cachef1 != &cachef2) {
            std::cerr << "Got two different cache instances(false): " << &cachent1 << "   " << &cachent2 << std::endl;
            result = 1;
        }
    }

    if (result != 0)
        return result;
    std::cout << "PASS: Tests for CacheManager" << std::endl;
    // Call basic test for every marshaller and every codec
    std::cout << "Basic Test with BasicMarshaller" << std::endl;
    {
        ConfigurationBuilder builder;
        builder.protocolVersion(argc > 1 ? argv[1] : Configuration::PROTOCOL_VERSION_28);
        builder.addServer().host(argc > 2 ? argv[2] : "127.0.0.1").port(argc > 3 ? atoi(argv[3]) : 11222);
//        builder.balancingStrategyProducer(transport::MyRoundRobinBalancingStrategy::newInstance);
        builder.balancingStrategyProducer(nullptr);
        RemoteCacheManager cacheManager(builder.build(), false);
        cacheManager.start();

        {
            std::cout << "Test different cache types" << std::endl;
            BasicMarshaller<float> *kmf = new BasicMarshaller<float>();
            BasicMarshaller<double> *vmd = new BasicMarshaller<double>();
            RemoteCache<float, double> cache1 = cacheManager.getCache<float, double>(kmf, &Marshaller<float>::destroy,
                    vmd, &Marshaller<double>::destroy,"default", true);
            float k1 = 1.0;
            double v1 = 1e-3;

            cache1.clear();

            // put
            cache1.put(k1, v1);
            std::unique_ptr<double> rv(cache1.get(k1));
            assert_not_null("get returned null!", __LINE__, rv);
            cache1.clear();
            if (*rv != v1) {
                std::cerr << "get/put fail for " << k1 << " got " << *rv << " expected " << v1 << std::endl;
                return 1;
            }
        }

        BasicMarshaller<std::string> *km = new BasicMarshaller<std::string>();
        BasicMarshaller<std::string> *vm = new BasicMarshaller<std::string>();
        RemoteCache<std::string, std::string> cache0 = cacheManager.getCache<std::string, std::string>(km,
                &Marshaller<std::string>::destroy, vm, &Marshaller<std::string>::destroy,"default", true);
        DataFormat<std::string, std::string> df;
        df.keyMediaType.typeSubtype = std::string("application/x-jboss-marshalling");
        df.valueMediaType.typeSubtype = std::string("application/x-jboss-marshalling");
        RemoteCache<std::string, std::string> cache = cache0.withDataFormat(&df);
        cache.clear();
            std::cout << "Test different cache types" << std::endl;

        try {
            result = basicTest<std::string, std::string>(cacheManager, cache);
            std::string argName1 = std::string("keyValue");
            std::string argValue1 = std::string("abc");
            std::string argName2 = std::string("keyName");
            std::string argValue2 = std::string("a");
            std::string script("// mode=local, language=javascript, parameters=[keyValue, keyName]\n"
                    "var cache = cacheManager.getCache(\"namedCache\");\n"
                    "cache.put(\"a\", keyValue);\n"
                    "cache.put(\"b\", \"b\");\n"
                    "cache.get(keyName);\n");
            std::string script_name("script.js");

            auto execution = cache.getRemoteExecution<>();
            execution.putScript(script_name, script);
            execution.addArg(argName1, argValue1);
            execution.addArg(argName2, argValue2);

            std::string* ret = execution.template execute<std::string*>(script_name);
            if (*ret != "abc") {
                std::cerr << "Task execution fails got " << *ret << " expected abc" << std::endl;
                return 1;
            }
            std::cout << "result is " << *ret << std::endl;
            result = 0;
        } catch (const Exception& e) {
            cacheManager.stop();
            std::cout << "is: " << typeid(e).name() << '\n';
            std::cerr << "fail unexpected exception: " << e.what() << std::endl;
            return 1;
        }
        std::cout << "PASS: script execution on server" << std::endl;

        cacheManager.stop();
    }
    if (result != 0)
        return result;

    std::cout << "Basic Test with JBasicMarshaller" << std::endl;
    {
        ConfigurationBuilder builder;
        //        builder.balancingStrategyProducer(transport::MyRoundRobinBalancingStrategy::newInstance);
        builder.balancingStrategyProducer(nullptr);
        builder.protocolVersion(argc > 1 ? argv[1] : Configuration::PROTOCOL_VERSION_28);
        builder.addServer().host(argc > 2 ? argv[2] : "127.0.0.1").port(argc > 3 ? atoi(argv[3]) : 11222);
        RemoteCacheManager cacheManager(builder.build(), false);
        cacheManager.start();

        JBasicMarshaller<std::string> *km = new JBasicMarshaller<std::string>();
        JBasicMarshaller<std::string> *vm = new JBasicMarshaller<std::string>();
        RemoteCache<std::string, std::string> cache0 = cacheManager.getCache<std::string, std::string>(km,
                &Marshaller<std::string>::destroy, vm, &Marshaller<std::string>::destroy, "default", true);
        DataFormat<std::string, std::string> df;
        df.keyMediaType.typeSubtype = std::string("application/x-jboss-marshalling");
        df.valueMediaType.typeSubtype = std::string("application/x-jboss-marshalling");
        RemoteCache<std::string, std::string> cache = cache0.withDataFormat(&df);
        try {
            result = basicTest<std::string, std::string>(cacheManager, cache);
            std::string argName1 = std::string("keyValue");
            std::string argValue1 = std::string("abc");
            std::string argName2 = std::string("keyName");
            std::string argValue2 = std::string("a");
            std::string argName3 = std::string("num");
            // execute() operation needs explicit JBossMarshalling<string> format for argument values
            std::string script("// mode=local, language=javascript, parameters=[keyValue, keyName, num]\n"
                    "var cache = cacheManager.getCache(\"namedCache\");\n"
                    "cache.put(\"a\", keyValue);\n"
                    "cache.put(\"b\", \"b\");\n"
                    "cache.get(keyName);\n"
                    "num;\n");
            std::string script_name("script1.js");

            auto execution = cache.getRemoteExecution<>();
            execution.putScript(script_name, script);
            execution.addArg(argName1, argValue1);
            execution.addArg(argName2, argValue2);
            execution.addArg(argName3, 2);

            int* ret = execution.template execute<int*>(script_name);
            std::cout << "result is " << *ret << std::endl;
            if (*ret != 2) {
                std::cerr << "Task execution fails got " << *ret << " expected 2" << std::endl;
                return 1;
            }

            // Checking execute with key
            int* ret1 = execution.template execute<int*>(script_name, argName1);
            std::cout << "result is " << *ret1 << std::endl;
            if (*ret1 != 2) {
                std::cerr << "Task execution fails got " << *ret1 << " expected 2" << std::endl;
                return 1;
            }

            // Checking old interface also
            auto jbmStr = JBasicMarshaller<std::string>();
            auto jbmInt = JBasicMarshaller<int>();
            std::vector<char> argVal1, argVal2,argVal3;
            jbmStr.marshall(argValue1, argVal1);
            jbmStr.marshall(argValue2, argVal2);
            jbmInt.marshall(2, argVal3);
            std::map<std::vector<char>,std::vector<char>> args;
            args[std::vector<char>(argName1.begin(), argName1.end())]=argVal1;
            args[std::vector<char>(argName2.begin(), argName2.end())]=argVal2;
            args[std::vector<char>(argName3.begin(), argName3.end())]=argVal3;
            auto ret2 = cache.execute(script_name, args, std::string("aKey"));
            std::vector<char> ret3(ret2.begin(), ret2.end());
            int* ret4 = jbmInt.unmarshall(ret3);
            std::cout << "result is " << *ret4 << std::endl;
            if (*ret4 != 2) {
                std::cerr << "Task execution fails got " << *ret4 << " expected 2" << std::endl;
                return 1;
            }
            return 0;
        } catch (const Exception& e) {
            cacheManager.stop();
            std::cout << "is: " << typeid(e).name() << '\n';
            std::cerr << "fail unexpected exception: " << e.what() << std::endl;
            return 1;
        }
        std::cout << "PASS: script execution on server" << std::endl;

        cacheManager.stop();
    }
    return result;
}

