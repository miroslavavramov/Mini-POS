# Mini-POS
# Compilation instructions:
# g++ -std=c++17 -o posgw pos_gateway.cpp

# Usage examples:

# 1. Start the payment gateway server:
#   ./posgw server --port 9000
# 2. Send a sale request from another terminal:
#   ./posgw sale --amount 12.34 --host 127.0.0.1 --port 9000

# Protocol:
# - Client sends: "SALE:<amount>"
# - Server responds: "SUCCESS:Transaction approved for $<amount>:TXN_ID_<timestamp>" or "ERROR:<message>"

