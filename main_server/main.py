from flask import Flask, request, jsonify

app = Flask(__name__)

@app.route('/data', methods=['POST'])
def receive_data():
    # Получаем данные из POST-запроса
    data = request.json
    
    # Выводим полученные данные
    print("Received data:", data)
    
    # Отправляем подтверждение
    return jsonify({"status": "success", "message": "Data received"}), 200

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=8080)
