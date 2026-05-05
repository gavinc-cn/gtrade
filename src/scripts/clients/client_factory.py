from clients.base_client import BaseClient
from clients.okex_v5_client import OkexV5Client


def client_factory(exchange: str, api_key: str, api_secret: str, http_proxy: str) -> BaseClient:
    client_map = {
        'okx_swap': OkexV5Client,
        'okx': OkexV5Client,
        'okx_dummy': OkexV5Client,
    }
    if exchange in client_map:
        return client_map[exchange](api_key=api_key, api_secret=api_secret, http_proxy=http_proxy)
    else:
        raise NotImplementedError(
            "Cannot create client instance: " +
            "{} client not implemented.".format(exchange))


client_dic = {}

def get_client(exchange, api_key, api_secret, http_proxy):
    global client_dic
    if (exchange, api_key, api_secret) not in client_dic:
        client_dic[(exchange, api_key, api_secret)] = client_factory(
            exchange, api_key, api_secret, http_proxy)
    return client_dic[(exchange, api_key, api_secret)]