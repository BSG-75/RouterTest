#include "Routers.hpp"
#include <boost/container_hash/hash.hpp>
#include <boost/asio/buffer.hpp>
#include <unordered_set>

using std::begin;
using std::end;
using std::exchange;
using std::move;
using std::swap;

namespace Tests
{
    class CoPromise
    {
    public:
        using Signature = void(std::exception_ptr);

        using AsyncResult = boost::asio::async_result<
            std::decay_t<decltype(boost::asio::use_awaitable)>,
            Signature
        >;
        using Awaitable = std::decay_t<AsyncResult::return_type>;
        using Handler = std::decay_t<AsyncResult::handler_type>;

    private:
        std::optional<Handler> m_handler;
        std::optional<Awaitable> m_awaitable;

    public:
        CoPromise()
        {
            using boost::asio::async_initiate;
            using boost::asio::use_awaitable;
            using Token = decltype(use_awaitable);

            auto const initiation = [this](Handler&& handler) 
            { 
                m_handler.emplace(move(handler)); 
            };
            m_awaitable.emplace
            (
                async_initiate<Token, Signature>(initiation, use_awaitable)
            );
        }

        ~CoPromise()
        {
            if (m_handler.has_value() and not m_awaitable.has_value())
            {
                reject(std::logic_error{ "unresolved promise" });
            }
        }

        Awaitable getAwaitable()
        {
            auto awaitable = exchange(m_awaitable, std::nullopt);
            if (not awaitable.has_value())
            {
                throw std::logic_error{ "copromise no state" };
            }
            return move(*awaitable);
        }

        void resolve()
        {
            throw std::runtime_error{ "not implemented" };
        }

        void reject(std::exception_ptr const& exception)
        {
            throw std::runtime_error{ "not implemented" };
        }

        void reject(std::exception const& exception)
        {
            throw std::runtime_error{ "not implemented" };
        }
    };

    Router::User::User(std::shared_ptr<Router>&& router, std::uint16_t const id)
    {
        if (router == nullptr)
        {
            throw std::invalid_argument{ "router is null" };
        }
        m_router = move(router);
        m_id = id;
    }

    Router::Awaitable<void> Router::User::send(EndPoint const& to, std::string_view const data) const
    {
        return m_router->send(m_id, to, data);
    }


    Router::Awaitable<std::pair<Router::EndPoint, std::string>> Router::User::receive() const
    {
        return m_router->receive(m_id);
    }

    Router::Router
    (
        Strand strand,
        Address const& address,
        std::unique_ptr<Nat> nat
    ) :
        m_lastId{ 0 },
        m_strand{ move(strand) },
        m_nat{ move(nat) }
    {}

    Router::User Router::createUser()
    {
        checkNat();
        m_lastId += 1;
        auto const userId = m_lastId;
        // todo: start receive
        return User{ shared_from_this(), userId };
    }

    Router::Awaitable<void> Router::send
    (
        std::uint16_t const port,
        EndPoint const& to,
        std::string_view const data
    )
    {
        auto const translated = m_nat->translate(port, to);
        auto const translatedPort = static_cast<unsigned short>(translated);
        auto [kv, emplaced] = m_sockets.try_emplace(translated, boost::asio::executor{ m_strand });
        auto& [key, socket] = *kv;
        if (emplaced)
        {
            auto const endPoint = EndPoint{ m_address, translatedPort };
            socket.open(endPoint.protocol());
            socket.bind(endPoint);
        }

        auto const buffer = boost::asio::buffer(data);
        auto const bytesSent = co_await socket.async_send_to(buffer, to);
        if (bytesSent != data.size())
        {
            throw std::runtime_error{ "buffer not completely sent" };
        }
    }

    Router::Awaitable<std::pair<Router::EndPoint, std::string>> Router::receive
    (
        std::uint16_t const port
    )
    {
        boost::asio::defer([] {});
        co_await boost::asio::defer(boost::asio::use_awaitable);
        co_return std::pair<Router::EndPoint, std::string>{};
    }

    Router::Socket& Router::getSocket
    (
        TranslatedID const translated,
        std::uint16_t const local
    )
    {
        checkNat();
        using SocketData = std::pair<Socket, std::optional<Awaitable<void>>>;
        auto [kv, emplaced] = m_sockets.try_emplace(translated, boost::asio::executor{ m_strand });
        auto& [key, socket] = *kv;
        if (emplaced)
        {
            auto const endPoint = EndPoint
            {
                m_address,
                static_cast<unsigned short>(translated)
            };
            socket.open(endPoint.protocol());
            socket.bind(endPoint);
            startReceive(translated, local);
        }
        return socket;
    }

    void Router::startReceive
    (
        TranslatedID const translated,
        std::uint16_t const local
    )
    {
        /*class ReceiveHandler
        {
        private:
            using Buffer = std::pair<std::string, EndPoint>;
            std::weak_ptr<Router> m_self;
            std::unique_ptr<Buffer> m_buffer;
            TranslatedID m_translated;
            std::uint16_t m_local;
            FutureProvider<UseQueue<std::string>> m_promise;
        public:

            static Future<UseQueue<std::string>> start
            (
                Router& self,
                TranslatedID const translated,
                std::uint16_t const local
            )
            {
                auto promise = FutureProvider<UseQueue<std::string>>{};
                auto const future = promise.getFuture();
                auto handler = ReceiveHandler
                {
                    self,
                    move(promise),
                    translated,
                    local
                };
                start(self, move(handler));
                return future;
            }

            void setResult(std::size_t bytesReceived)
            {
                checkState();
                auto const self = m_self.lock();
                if (self == nullptr)
                {
                    return;
                }

                try
                {
                    self->checkNat();
                    auto const& remote = getEndPoint();
                    auto const id = self->m_nat->translate(m_translated, remote);
                    if (not id.has_value())
                    {
                        auto promise = m_promise;
                        try
                        {
                            start(*self, move(*this));
                        }
                        catch (...)
                        {
                            promise.setException(std::current_exception());
                        }
                        return;
                    }
                    if (id.value() != m_local)
                    {
                        throw std::logic_error{ "not implemented yet" };
                    }
                    auto& result = getString();
                    result.resize(bytesReceived);
                    m_promise.setResult(std::move(result));
                }
                catch (...)
                {
                    setException(std::current_exception());
                }
            }

            void setException(std::exception_ptr const& exception)
            {
                checkState();
                m_promise.setException(std::current_exception());
            }

            std::string& getString() const
            {
                checkState();
                return std::get<std::string>(*m_buffer);
            }

            boost::asio::const_buffer getBuffer() const
            {
                checkState();
                return boost::asio::buffer(getString());
            }

            EndPoint& getEndPoint() const
            {
                checkState();
                return std::get<EndPoint>(*m_buffer);
            }
        private:
            ReceiveHandler
            (
                Router& self,
                FutureProvider<UseQueue<std::string>> promise,
                TranslatedID const translated,
                std::uint16_t const local
            ) :
                m_self{ self.weak_from_this() },
                m_buffer{ std::make_unique<Buffer>() },
                m_translated{ translated },
                m_local{ local }
            {
                getString().resize(512);
            }

            static void start(Router& self, ReceiveHandler&& handler)
            {
                handler.checkState();
                auto const future = self.m_sockets.at(handler.m_translated)->async_receive_from
                (
                    handler.getBuffer(),
                    handler.getEndPoint(),
                    self.m_helper
                );
                future.then(move(handler));
            }

            void checkState() const
            {
                if (m_buffer == nullptr)
                {
                    throw std::logic_error{ "no state" };
                }
            }

            std::optional<FutureProvider<UseQueue<std::string>>> translate()
            {
                checkState();
                auto const self = m_self.lock();
                if (self == nullptr)
                {
                    return std::nullopt;
                }
                self->checkNat();
                auto const local =
                    self->m_nat->translate(m_translated, getEndPoint());
                if (not local.has_value())
                {
                    return std::nullopt;
                }
                if (local.value() != m_local)
                {
                    throw std::logic_error{ "not expected" };
                }
                return m_promise;
            }
        };*/

        /*auto [futureKv, futureSet] = m_received.try_emplace
        (
            local,
            ReceiveHandler::start(*this, translated, local)
        );
        if (not futureSet)
        {
            throw std::logic_error{ "failed to save future" };
        }*/
    }

    void Router::checkNat() const
    {
        if (m_nat == nullptr)
        {
            throw std::logic_error{ "no nat" };
        }
    }

    template<typename T>
    class NatImplementation final : public Router::Nat
    {
    public:
        using TranslatedID = Router::TranslatedID;
        using EndPoint = Router::EndPoint;

    private:
        using NatTableKey = typename T::NatTableKey;
        using Hash = typename T::Hash;

    private:
        T m_nat;
        std::unordered_map<NatTableKey, TranslatedID, Hash> m_natTable;
        std::unordered_map<TranslatedID, NatTableKey> m_reverseNatTable;
        std::uint16_t m_counter = 10000;

    private:
        std::optional<TranslatedID> translate(std::uint16_t const local) override
        {
            return std::nullopt;
        }

        TranslatedID translate
        (
            std::uint16_t const local,
            EndPoint const& remote
        ) override
        {
            m_nat.processKeyData(local, remote);
            auto const key = m_nat.toKey(local, remote);
            if (auto const found = m_natTable.find(key); found != cend(m_natTable))
            {
                return std::get<TranslatedID>(*found);
            }

            if (m_counter > 50000)
            {
                throw std::overflow_error{ "Nat table exhausted" };
            }
            m_counter += 100;
            auto const translated = static_cast<TranslatedID>(m_counter);
            m_natTable[key] = translated;
            m_reverseNatTable[translated] = key;
            return translated;
        }

        std::optional<std::uint16_t> translate
        (
            TranslatedID const translated,
            EndPoint const& remote
        ) override
        {
            auto const found = m_reverseNatTable.find(translated);
            if (found == cend(m_reverseNatTable))
            {
                return std::nullopt;
            }

            auto const& key = std::get<NatTableKey>(*found);
            if (not m_nat.allowTranslate(key, remote))
            {
                return std::nullopt;
            }
            return m_nat.localFromKey(key);
        }
    };

    template<typename T>
    class ConeNat
    {
    public:
        using NatTableKey = std::uint16_t;
        using Hash = std::hash<NatTableKey>;
        using EndPoint = Router::EndPoint;

    private:
        T m_nat;

    public:
        void processKeyData(std::uint16_t const local, EndPoint const& remote)
        {
            return m_nat.processKeyData(local, remote);
        }

        bool allowTranslate(NatTableKey const& key, EndPoint const& remote) const
        {
            return m_nat.allowTranslate(key, remote);
        }

        NatTableKey toKey(std::uint16_t const local, EndPoint const& remote) const
        {
            return local;
        }

        std::uint16_t localFromKey(NatTableKey const& key) const
        {
            return key;
        }
    };

    template<typename T>
    class RestrictedNat
    {
    private:
        using CacheValue = typename T::CacheValue;
        using Hash = typename T::Hash;
        using EndPoint = Router::EndPoint;
        using Key = typename ConeNat<RestrictedNat<T>>::NatTableKey;

    private:
        T m_nat;
        std::unordered_map<std::uint16_t, std::unordered_set<CacheValue, Hash>> m_cache;

    public:
        void processKeyData(std::uint16_t const local, EndPoint const& remote)
        {
            m_cache[local].insert(m_nat.fromEndPoint(remote));
        }

        bool allowTranslate(Key const& key, EndPoint const& remote) const
        {
            auto const found = m_cache.find(key);
            if (found == cend(m_cache))
            {
                return false;
            }
            auto const& [foundKey, set] = *found;
            auto const value = m_nat.fromEndPoint(remote);
            return set.count(value);
        }
    };

    struct AddressHash
    {
        std::size_t operator()(Router::Address const& key) const
        {
            auto hash = static_cast<std::size_t>(0);
            if (key.is_v4())
            {
                boost::hash_combine(hash, key.to_v4().to_uint());
            }
            else
            {
                boost::hash_combine(hash, key.to_v6().to_bytes());
            }
            return hash;
        }

        std::size_t operator()(Router::EndPoint const& endPoint) const
        {
            auto hash = operator()(endPoint.address());
            boost::hash_combine(hash, endPoint.port());
            return hash;
        }
    };

    class Router::NoNat final : public Nat
    {
        std::optional<TranslatedID> translate(std::uint16_t const local)
        {
            return TranslatedID{ local };
        }

        TranslatedID translate
        (
            std::uint16_t const local,
            EndPoint const& remote
        ) override
        {
            return TranslatedID{ local };
        }

        std::optional<std::uint16_t> translate
        (
            TranslatedID const translated,
            EndPoint const& remote
        ) override
        {
            return static_cast<std::uint16_t>(translated);
        }
    };

    class Router::FullCone
    {
    public:
        void processKeyData(...) {}
        bool allowTranslate(...) const { return true; }
    };

    class Router::AddressRestricted
    {
    public:
        using CacheValue = Address;
        using Hash = AddressHash;
    public:
        CacheValue fromEndPoint(EndPoint const& remote) const
        {
            return remote.address();
        }
    };

    class Router::PortRestricted
    {
    public:
        using CacheValue = Router::EndPoint;
        using Hash = AddressHash;
    public:
        CacheValue fromEndPoint(EndPoint const& remote) const
        {
            return remote;
        }
    };

    class Router::Symmetric
    {
    public:
        using NatTableKey = std::pair<std::uint16_t, EndPoint>;
        struct Hash
        {
            std::size_t operator()(NatTableKey const& key) const
            {
                auto hash = AddressHash{}(std::get<EndPoint>(key));
                boost::hash_combine(hash, key.first);
                return hash;
            }
        };

        void processKeyData(std::uint16_t const, EndPoint const&) {}

        NatTableKey toKey(std::uint16_t const local, EndPoint const& remote) const
        {
            return std::pair{ local, remote };
        }

        std::uint16_t localFromKey(NatTableKey const& key) const
        {
            return std::get<std::uint16_t>(key);
        }

        bool allowTranslate(NatTableKey const&, EndPoint const&) const
        {
            return true;
        }
    };

    template<>
    std::unique_ptr<Router::Nat> Router::makeNat<Router::NoNat>()
    {
        return std::make_unique<Router::NoNat>();
    }

    template<>
    std::unique_ptr<Router::Nat> Router::makeNat<Router::FullCone>()
    {
        return std::make_unique<NatImplementation<ConeNat<FullCone>>>();
    }

    template<>
    std::unique_ptr<Router::Nat> Router::makeNat<Router::AddressRestricted>()
    {
        return std::make_unique<NatImplementation<ConeNat<RestrictedNat<AddressRestricted>>>>();
    }

    template<>
    std::unique_ptr<Router::Nat> Router::makeNat<Router::PortRestricted>()
    {
        return std::make_unique<NatImplementation<ConeNat<RestrictedNat<PortRestricted>>>>();
    }

    template<>
    std::unique_ptr<Router::Nat> Router::makeNat<Router::Symmetric>()
    {
        return std::make_unique<NatImplementation<Symmetric>>();
    }

}