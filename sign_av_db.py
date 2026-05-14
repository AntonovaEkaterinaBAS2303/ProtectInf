import struct
import hashlib
from pathlib import Path
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa
from cryptography.hazmat.backends import default_backend
from datetime import datetime

def load_private_key(filepath):
    """Загрузка приватного ключа в разных форматах"""
    with open(filepath, 'rb') as f:
        key_data = f.read()
    
    # Пробуем PKCS#8
    try:
        return serialization.load_pem_private_key(key_data, password=None, backend=default_backend())
    except:
        pass
    
    # Пробуем PKCS#1 (RSA PRIVATE KEY)
    try:
        return serialization.load_pem_private_key(
            b"-----BEGIN RSA PRIVATE KEY-----\n" + key_data + b"\n-----END RSA PRIVATE KEY-----",
            password=None,
            backend=default_backend()
        )
    except:
        pass
    
    raise ValueError("Cannot load private key")

def load_public_key(filepath):
    """Загрузка публичного ключа"""
    with open(filepath, 'rb') as f:
        key_data = f.read()
    
    try:
        return serialization.load_pem_public_key(key_data, backend=default_backend())
    except:
        return None

def sign_with_private_key(data, private_key):
    """Подпись данных"""
    if isinstance(private_key, rsa.RSAPrivateKey):
        signature = private_key.sign(
            data,
            padding.PKCS1v15(),
            hashes.SHA256()
        )
        return signature
    raise ValueError("Invalid key type")

def calculate_hash(data):
    return hashlib.sha256(data).digest()

def create_manifest(record_count, timestamp=None):
    """Создание манифеста"""
    magic = b'AVDB'
    version = 1
    release_ts = timestamp or int(datetime.now().timestamp())
    
    data = bytearray()
    data.extend(magic)
    data.extend(struct.pack('<I', version))
    data.extend(struct.pack('<Q', release_ts))
    data.extend(struct.pack('<I', record_count))
    return bytes(data)

def create_record_data(sig_prefix, sig_data, offset_begin, offset_end, object_type, private_key, sign=True):
    """Создание записи с опциональной подписью"""
    sig_len = len(sig_data)
    hash_data = calculate_hash(sig_data)
    
    record = bytearray()
    record.extend(struct.pack('<Q', sig_prefix))
    record.extend(struct.pack('<I', sig_len))
    record.extend(hash_data)
    record.extend(struct.pack('<Q', offset_begin))
    record.extend(struct.pack('<Q', offset_end))
    record.extend(struct.pack('<I', object_type))
    
    if sign and private_key:
        signature = sign_with_private_key(bytes(record), private_key)
        record.extend(struct.pack('<I', len(signature)))
        record.extend(signature)
    else:
        record.extend(struct.pack('<I', 0))  # 0 длина подписи
    
    return bytes(record)

def create_full_database(output_path, private_key_path, variant='valid'):
    """Создание базы данных"""
    private_key = load_private_key(private_key_path)
    
    # Тестовые записи
    records_config = [
        (0x41402550214F3558, b'X5O!P%@A', 0, 68, 1, 'EICAR'),
        (0x53207265776F5000, b'PowerShe', 0, 23, 6, 'PowerShell'),
        (0x206F6863654045, b'@echo Vi', 0, 16, 1, 'BAT')
    ]
    
    # Манифест
    manifest_data = create_manifest(len(records_config))
    
    if variant == 'valid':
        manifest_sig = sign_with_private_key(manifest_data, private_key)
        sign_records = True
    elif variant == 'invalid_manifest':
        manifest_sig = b'\x00' * 256  # Невалидная подпись
        sign_records = True
    elif variant == 'unsigned_records':
        manifest_sig = sign_with_private_key(manifest_data, private_key)
        sign_records = False
    else:
        manifest_sig = b''
        sign_records = False
    
    with open(output_path, 'wb') as f:
        # Заголовок
        f.write(b'AVDB')
        f.write(struct.pack('<I', 1))
        f.write(struct.pack('<Q', int(datetime.now().timestamp())))
        f.write(struct.pack('<I', len(records_config)))
        f.write(struct.pack('<I', 0))  # reserved
        
        # Подпись манифеста
        f.write(struct.pack('<I', len(manifest_sig)))
        f.write(manifest_sig)
        
        # Записи
        for sig_prefix, sig_data, off_begin, off_end, obj_type, name in records_config:
            record_bytes = create_record_data(
                sig_prefix, sig_data, off_begin, off_end, obj_type,
                private_key if sign_records else None,
                sign_records
            )
            f.write(record_bytes)
    
    print(f"База '{variant}' создана: {output_path}")
    return True

if __name__ == '__main__':
    private_key_path = 'private_key.pem'
    if not Path(private_key_path).exists():
        private_key_path = 'private_key_new.pem'
    
    if not Path(private_key_path).exists():
        print("Приватный ключ не найден!")
        exit(1)
    
    create_full_database('av_database_valid.bin', private_key_path, 'valid')
    create_full_database('av_database_invalid.bin', private_key_path, 'invalid_manifest')
    create_full_database('av_database_mixed.bin', private_key_path, 'unsigned_records')