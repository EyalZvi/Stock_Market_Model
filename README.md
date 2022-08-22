# Stock Market Model

**Our project is a representation of a real-time stock market application using C socket programing.**

Clients will be able to register to an open server with a unique username
and password and will then be assigned a unique stock portfolio that 
belongs to them.
Clients that have already registered will be able to reconnect and continue
with their portfolio state as long as the server wasn't shut down in the 
process.
Every client will be able to buy and sell stocks with his money, and 
observe the best current offers and price per unit of each stock.
Every transaction in a stock triggers a change in the stock price 
(price of unit per stock), a sell and a buy with a higher unit price than the 
current unit increases the stock price and vice versa.

Both TCP and UDP will be used for each client-server connection,
We will use TCP's reliability to preform reliable transactions. 
By using TCP, we ensure QoS, each transaction will (eventually) be 
handled without any losses, every offer is added to the relevant array.
We will use UDP to perform real-time updates using multicast, these 
updates include changing stock prices and best current prices for them.
we will add a sequence number to the UDP packets in order to reliably 
keep the clients up to date with relevant new updates and not ones that 
were for some reason delayed (they will be dropped).
