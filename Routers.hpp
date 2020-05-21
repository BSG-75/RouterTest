
#pragma once
#include <boost/asio.hpp>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace Tests
{
    class Router : std::enable_shared_from_this<Router>
    {
    public:
        template<typename T>
        using Awaitable = boost::asio::awaitable<T>;
        using Strand = boost::asio::strand<boost::asio::executor>;
        using UDP = boost::asio::ip::udp;
        using Socket = boost::asio::use_awaitable_t<>::as_default_on_t<UDP::socket>;
        using Address = boost::asio::ip::address;
        using EndPoint = UDP::endpoint;

        class User;

        enum class TranslatedID : std::uint16_t {};
        class Nat;

        class NoNat;
        class FullCone;
        class AddressRestricted;
        class PortRestricted;
        class Symmetric;

    private:
        Strand m_strand;
        Address m_address;
        std::unordered_map<TranslatedID, Socket> m_sockets;
        std::unique_ptr<Nat> m_nat;
        std::uint16_t m_lastId;

    public:
        template<typename T>
        static std::shared_ptr<Router> create
        (
            Strand strand,
            Address const& address
        );
        Router
        (
            Strand strand,
            Address const& address,
            std::unique_ptr<Nat> m_nat
        );
        User createUser();
        Awaitable<void> send
        (
            std::uint16_t const port,
            EndPoint const& to,
            std::string_view const data
        );
        Awaitable<std::pair<EndPoint, std::string>> receive(std::uint16_t const port);
    private:
        template<typename T>
        static std::unique_ptr<Nat> makeNat();

        Socket& getSocket
        (
            TranslatedID const translated,
            std::uint16_t const local
        );

        void startReceive
        (
            TranslatedID const translated,
            std::uint16_t const local
        );

        void checkNat() const;
    };

    class Router::User
    {
    private:
        std::shared_ptr<Router> m_router;
        std::uint16_t m_id;

    public:
        int getId() const noexcept;
        User(std::shared_ptr<Router>&& router, std::uint16_t const id);
        Awaitable<void> send(EndPoint const& to, std::string_view const data) const;
        Awaitable<std::pair<EndPoint, std::string>> receive() const;
    };

    class Router::Nat
    {
    public:
        virtual ~Nat() = default;
        virtual std::optional<TranslatedID> translate(std::uint16_t const local) = 0;
        virtual TranslatedID translate
        (
            std::uint16_t const local,
            EndPoint const& remote
        ) = 0;
        virtual std::optional<std::uint16_t> translate
        (
            TranslatedID const translated,
            EndPoint const& remote
        ) = 0;
    };

    template<typename T>
    static std::shared_ptr<Router> Router::create
    (
        Strand strand,
        Address const& address
    )
    {
        return std::make_shared<Router>
        (
            std::move(strand),
            address,
            makeNat<T>()
        );
    }

    template<typename T>
    std::unique_ptr<Router::Nat> Router::makeNat()
    {
        static_assert(false, "Invalid type");
    }

    template<>
    std::unique_ptr<Router::Nat> Router::makeNat<Router::NoNat>();
    extern template std::unique_ptr<Router::Nat> Router::makeNat<Router::NoNat>();
    template<>
    std::unique_ptr<Router::Nat> Router::makeNat<Router::FullCone>();
    extern template std::unique_ptr<Router::Nat> Router::makeNat<Router::FullCone>();
    template<>
    std::unique_ptr<Router::Nat> Router::makeNat<Router::AddressRestricted>();
    extern template std::unique_ptr<Router::Nat> Router::makeNat<Router::FullCone>();
    template<>
    std::unique_ptr<Router::Nat> Router::makeNat<Router::PortRestricted>();
    extern template std::unique_ptr<Router::Nat> Router::makeNat<Router::PortRestricted>();
    template<>
    std::unique_ptr<Router::Nat> Router::makeNat<Router::Symmetric>();
    extern template std::unique_ptr<Router::Nat> Router::makeNat<Router::Symmetric>();
}