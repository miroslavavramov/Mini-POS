# Mini-POS
# Compilation instructions:
# g++ -std=c++17 -o posgw pos_gateway.cpp -lsqlite3

# Usage examples:

# 1. Start the payment gateway server:
#   ./posgw server --port 9000
# 2. Send a sale request from another terminal:
#   ./posgw sale --amount 12.34 --host 127.0.0.1 --port 9000
# ./posgw sale --amount 75.00 --host 127.0.0.1 --port 9000

# OPI-Lite Protocol:
# - Handshake:
#  * Client → Terminal: "HELLO|GW|1.0"
#  * Terminal → Client: "HELLO|TERM|1.0"
# - Sale request:
#  * Client → Terminal: "AUTH|<amount>|<unix_ts>|<nonce>"
#  * Terminal → Client: "APPROVED|<auth_code>|<masked_pan>|<rrn>" or "DECLINED|<reason>"
# - Keepalive:
#  * Client → Terminal: "PING" (if no response for 3 seconds)
#  * Terminal → Client: "PONG"

