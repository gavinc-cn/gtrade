"""设置页服务：标的范围（经 gtrade_client 转发 gtrade HTTP Gateway，不落库不写文件）"""
import gtrade_client


def get_instrument_scope():
    """页面数据：全量标的 + 当前范围 + 订阅现状；引擎不可达抛 RuntimeError"""
    list_body, list_status = gtrade_client.forward('GET', '/api/instrument/list')
    scope_body, scope_status = gtrade_client.forward('GET', '/api/instrument/scope')
    if list_status != 200 or scope_status != 200:
        raise RuntimeError("引擎返回异常")
    return {
        'instruments': list_body.get('data', []),
        'scope': scope_body.get('scope', []),
        'owned': scope_body.get('owned', []),
    }


def set_instrument_scope(scope):
    """转发保存请求；引擎不可达抛 RuntimeError（EngineUnavailable 为其子类）"""
    return gtrade_client.forward('POST', '/api/instrument/scope', json_body={'scope': scope})
