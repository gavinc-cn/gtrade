import jwt
from datetime import datetime, timedelta
from functools import wraps
from flask import request, jsonify, current_app
import logging

logger = logging.getLogger(__name__)

def generate_token(username):
    """
    生成 JWT token

    Args:
        username: 用户名

    Returns:
        str: JWT token
    """
    expiration = datetime.utcnow() + timedelta(hours=current_app.config['JWT_EXPIRATION_HOURS'])

    payload = {
        'username': username,
        'exp': expiration,
        'iat': datetime.utcnow()
    }

    token = jwt.encode(payload, current_app.config['JWT_SECRET_KEY'], algorithm='HS256')
    return token

def verify_token(token):
    """
    验证 JWT token

    Args:
        token: JWT token

    Returns:
        dict: 包含用户信息的字典，验证失败返回 None
    """
    try:
        payload = jwt.decode(token, current_app.config['JWT_SECRET_KEY'], algorithms=['HS256'])
        return payload
    except jwt.ExpiredSignatureError:
        logger.warning("Token 已过期")
        return None
    except jwt.InvalidTokenError as e:
        logger.warning(f"无效的 token: {e}")
        return None

def token_required(f):
    """
    需要 token 认证的装饰器

    用法:
        @app.route('/api/protected')
        @token_required
        def protected_route():
            return jsonify({'message': 'success'})
    """
    @wraps(f)
    def decorated(*args, **kwargs):
        token = None

        # 从请求头中获取 token
        if 'Authorization' in request.headers:
            auth_header = request.headers['Authorization']
            # 格式: "Bearer <token>"
            try:
                token = auth_header.split(' ')[1]
            except IndexError:
                return jsonify({'success': False, 'message': '无效的认证格式'}), 401

        if not token:
            return jsonify({'success': False, 'message': '缺少认证令牌'}), 401

        # 验证 token
        payload = verify_token(token)
        if payload is None:
            return jsonify({'success': False, 'message': '认证令牌无效或已过期'}), 401

        # 将用户信息添加到 request 对象
        request.current_user = payload

        return f(*args, **kwargs)

    return decorated

def authenticate(username, password):
    """
    验证用户名和密码

    Args:
        username: 用户名
        password: 密码

    Returns:
        bool: 验证成功返回 True，否则返回 False
    """
    return (username == current_app.config['WEB_USER'] and
            password == current_app.config['WEB_PASSWORD'])
