# Feed handler configuration

One file per Layer 1 feed handler **process**, mirroring `config/gateways/`.

Layer legend (overview.md, "The channels"): L1 = ingestion (feed handler), L2 = strategy,
L3 = risk (firewall), L4 = OMS (ledger), L5 = EMS (execution), L6 = observability.

The reasoning for the one-file-per-process rule — fault isolation, per-process
core pinning, independent reconnection — is identical for both layers and is
written up once in [`../gateways/README.md`](../gateways/README.md). Read that
first; this directory only differs in what the file describes.

```
config/feeds/
  binance.yaml      # venue: binance, producer_id: 1, core: 2
  README.md
  # coinbase.yaml   # venue: coinbase, producer_id: 200, core: 8  (when the adapter exists)
  #   producer_id 200-255 is the range for additional FEED HANDLERS; gateways
  #   use 256-299 (contracts.md, "The message header"). The ranges are separate on purpose.
```

## Difference from `config/gateways/`

|                    | `config/feeds/`                              | `config/gateways/`               |
| ------------------ | -------------------------------------------- | -------------------------------- |
| Layer              | L1 ingestion                                 | L5 execution                     |
| Direction          | inbound market data                          | outbound orders, inbound reports |
| Credentials        | none for public feeds                        | required                         |
| Carries            | stream subscriptions, book depth, gap policy | order endpoints, rate limits     |
| Adds to the system | canonical ids **published**                  | canonical ids **tradable**       |

A venue typically appears in both, as two separate processes. Binance market
data does not stop flowing because the Binance order session is reconnecting,
and vice versa; keeping them in separate processes with separate configs is what
guarantees that.

## Adding a venue

1. Add the venue's listing to `config/instruments.yaml` under `venues:<name>`
   (tick size, step size, min notional — venue properties, not asset properties).
2. Copy `binance.yaml` to `<venue>.yaml` and change `venue`, the websocket URL
   and `streams`, `producer_id`, and `core`. Each must be unique per process.
3. Add a matching systemd unit under `deploy/systemd/`.

## Note on `streams`

Stream names are **venue symbols** (`btcusdt@trade`), not canonical ids. The
feed handler resolves them to canonical ids once at ingress; everything it
publishes carries only the integer id. This is the boundary that keeps L2 (strategy) and L3 (risk)
venue-agnostic.
