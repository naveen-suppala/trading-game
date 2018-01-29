#include <crow/crow_all.h>

#include <exchange_market.h>
#include <exchange_randomtrader.h>
#include <exchange_arbitragedestroyer.h>
#include <exchange_brokerage.h>
#include <thread>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>

std::string read_file(std::string filename)
{
    std::stringstream ss;
    std::ifstream fr(filename);
    std::string line;
    while (getline(fr, line)) ss << line << "\n";
    return ss.str();
}

int main()
{
    using namespace exchange;
    using namespace std::chrono_literals;

    auto config = crow::json::load(read_file("config.json"));

    Market::TradedPairs traded_pairs{
        Market::CP{new Currency{"EUR", "USD", 2ll, 12427, 250'000'000'000}},
        Market::CP{new Currency{"GBP", "USD", 4ll, 14170, 68'000'000'000}},
        Market::CP{new Currency{"USD", "JPY", 4ll, 1088139, 64'000'000'000}},
        Market::CP{new Currency{"EUR", "JPY", 6ll, 1352231, 30'000'000'000}},
        Market::CP{new Currency{"EUR", "GBP", 4ll, 8770, 20'000'000'000}},
        Market::CP{new Currency{"GBP", "JPY", 8ll, 1541926, 5'000'000'000}},
    };
    Market market {traded_pairs, "USD"};

    RandomTrader trader {market, config["mover_trade_size"].i()};
    std::thread market_mover([&]{
        while (true) {
            trader.trade();
            auto interval = std::chrono::milliseconds(config["trade_interval"].i());
            std::this_thread::sleep_for(interval);
        }
    });

    std::thread arbitrage_destroyer([&]{
        while (true) {
            ArbitrageDestroyer::normalise(market);
            auto interval = std::chrono::milliseconds(config["arbitrage_interval"].i());
            std::this_thread::sleep_for(interval);
        }
    });

    auto currency = config["currency"].s();

    Brokerage brokerage {{
        {"api_key1", Brokerage::A{new Account{"Kondratiy", {{currency, config["init_amount"].d()}}, market}}},
        {"api_key2", Brokerage::A{new Account{"Potap", {{currency, config["init_amount"].d()}}, market}}},
    }, config["fee_amount"].d(), currency};

    //--------------------------------------------------------------------------
    crow::SimpleApp app;

    CROW_ROUTE(app, "/")([](){
        crow::mustache::context x;
    
        auto page = crow::mustache::load("index.html");
        return page.render(x);
    });

    CROW_ROUTE(app, "/market")([&market]{
        crow::json::wvalue x;
        const auto quotes = market.get_all_quotes();
        for (const auto& quote: quotes) {
            x[quote.ccy_pair] = quote.to_string();
        }
        return x;
    });

    CROW_ROUTE(app, "/accounts")([&brokerage]{
        crow::json::wvalue x;
        const auto accounts = brokerage.accounts_under_management("GBP");
        for (const auto& [name, balance]: accounts) {
            x[name] = round(balance);
        }
        return x;
    });

    CROW_ROUTE(app, "/account/<string>")([&brokerage](const auto& api_key){
        crow::json::wvalue x;
        try {
            const auto holdings = brokerage.get_holdings(api_key);
            for (const auto& [ccy, balance]: holdings) {
                x[ccy] = balance;
            }
        }
        catch (std::runtime_error& e) {
            x["error"] = e.what();
        }
        return x;
    });

    CROW_ROUTE(app, "/trade/<string>/<string>/<string>/<double>")
    ([&brokerage](const auto& api_key,
                  const auto& direction,
                  const auto& ccy_pair,
                  double amount) {
        crow::json::wvalue x;
        try {
            if (ccy_pair.size() != 6) throw std::runtime_error("Wrong ccy pair");
            const auto ccy1 = ccy_pair.substr(0, 3);
            const auto ccy2 = ccy_pair.substr(3);

            const auto holdings = (direction == "buy")
                                ? brokerage.buy(api_key, amount, ccy1, ccy2)
                                : brokerage.sell(api_key, amount, ccy1, ccy2);

            for (const auto& [ccy, balance]: holdings) {
                x[ccy] = balance;
            }
        }
        catch (std::runtime_error& e) {
            x["error"] = e.what();
        }
        return x;
    });

    app.port(18080).multithreaded().run();
}

