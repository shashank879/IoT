import os
import time
import json
import logging
import threading
from enum import Enum
import paho.mqtt.client as paho
from utils.periodic import Periodic

SERVICE_STATUS = Enum("SERVICE_STATUS",
                      ["CONNECTED", "STARTED", "PAUSED", "STOPPED", "DISCONNECTED"])


class BaseMQTTService(object):

    QOS = 0
    clean_session = False

    def __init__(self,
                 name,
                 password,
                 address='192.168.0.100',
                 port=1883,
                 keepalive=600,
                 clean_session=True,
                 autostart=False):
        assert name.startswith('service/'), 'Name of a service should begin with "service/"'

        dir_path = os.path.dirname(os.path.realpath(__file__))
        log_dir = os.path.join(dir_path, 'logs')
        if not os.path.exists(log_dir):
            os.makedirs(log_dir)
        log_file = os.path.join(log_dir, '{}.log'.format(name.replace('/', '_')))
        logging.basicConfig(filename=log_file,
                            level=logging.INFO)
        self.name = name
        self.address = address
        self.port = port
        self.keepalive = keepalive
        self.clean_session = clean_session
        self.autostart = autostart
        self.status = SERVICE_STATUS.DISCONNECTED

        self.client = paho.Client(name, clean_session=clean_session)

        self.client.username_pw_set(name, password)
        self.client.on_connect = self.on_connect
        self.client.on_disconnect = self.on_disconnect
        self.client.on_publish = self.on_publish
        self.client.on_message = self.on_message

    def __call__(self, **kwargs):
        logging.info('[*] Initializing service')
        self.init_service(**kwargs)
        logging.info('[*] Service initialized!')
        self.client.connect(self.address,
                            port=self.port,
                            keepalive=self.keepalive // 2)
        self.client.subscribe('request/service', qos=2)
        self.client.loop_start()
        logging.info('[*] MQTT loop started')
        while True:
            self.loop()
            # self.check_requirement()

    @property
    def service_info(self):
        return None

    def on_connect(self, client, userdata, flags, rc):
        if rc==0:
            logging.info("[*] Service Connected OK. Returned code=%s", rc)
            self.status = SERVICE_STATUS.CONNECTED
            if self.service_info:
                info_str = json.dumps(self.service_info)
                client.publish("connected/service", info_str)

            if self.autostart:
                try:
                    logging.info("[*] Starting service!")
                    self.start_service()
                    self.resume_service()
                    self.status = SERVICE_STATUS.STARTED
                    self.last_service_request_time = time.time()
                    logging.info("[*] Started service!")
                except Exception as e:
                    logging.error(e)

        else:
            logging.error("Bad connection Returned code=%s", rc)

    def on_disconnect(self, client, userdata, rc):
        if rc==0:
            logging.info("Service Disconnected OK. Returned code=%s", rc)
            self.status = SERVICE_STATUS.DISCONNECTED
            if self.service_info:
                info_str = json.dumps(self.service_info)
                client.publish("client_disconnected", info_str)
        else:
            logging.error("Bad disconnection Returned code=", rc)
        logging.info("[*] Stopping service!")
        self.stop_service()
        logging.info("[*] Stopped service!")

    def on_publish(self, client, userdata, mid):
        # logging.debug('mid={} successfully published'.format(mid))
        return 0

    def on_message(self, client, userdata, message):
        topic = message.topic
        data = json.loads(str(message.payload.decode("utf-8")))
        logging.debug('message received on topic:{}, data:{}'.format(topic, json.dumps(data)))
        if topic == 'request/service' and data["service_name"] == self.name:
            self.last_service_request_time = time.time()
            if self.status == SERVICE_STATUS.PAUSED:
                logging.info("[*] Resuming service!")
                self.resume_service()
                self.status = SERVICE_STATUS.STARTED
                logging.info("[*] Resumed service!")
            elif self.status == SERVICE_STATUS.STOPPED:
                logging.info("[*] Starting service!")
                self.start_service()
                self.resume_service()
                self.status = SERVICE_STATUS.STARTED
                logging.info("[*] Started service!")
        else:
            self.receive(topic, data)

    def check_requirement(self):
        current_time = time.time()
        if (self.status == SERVICE_STATUS.STARTED and \
            current_time - self.last_service_request_time > self.keepalive):
            logging.info("[*] Pausing service!")
            self.pause_service()
            self.status = SERVICE_STATUS.PAUSED
            logging.info("[*] Paused service!")
        if (self.status == SERVICE_STATUS.STARTED or self.status == SERVICE_STATUS.PAUSED) and \
            (current_time - self.last_service_request_time > self.keepalive * 2):
            logging.info("[*] Stopping service!")
            self.stop_service()
            self.status = SERVICE_STATUS.STOPPED
            logging.info("[*] Stopped service!")

    def init_service(self, *args, **kwargs):
        return 0

    def start_service(self):
        return 0

    def resume_service(self):
        return 0

    def pause_service(self):
        return 0

    def stop_service(self):
        return 0

    def receive(self, client_name, data):
        return

    def receive_direct_message(self):
        pass

    def publish(self, data):
        self.client.publish('data/' + self.name,
                            payload=json.dumps(data),
                            qos=self.QOS)

    def loop(self):
        return 0


class ByteStreamService(BaseMQTTService):

    MAX_FPS = 60

    def init_service(self, fps=60):
        super(ByteStreamService, self).init_service()
        self.fps = min(self.MAX_FPS, fps)
        # self.timer = Periodic(self.step, 1 / self.fps)
        return 0

    def start_service(self):
        super(ByteStreamService, self).start_service()
        self.last_update = time.time()
        # self.timer.start()
        return 0

    # def resume_service(self):
    #     super().resume_service()
    #     self.timer.start()
    #     return 0
    
    # def pause_service(self):
    #     super().pause_service()
    #     self.timer.stop()
    #     return 0

    # def stop_service(self):
    #     return super().stop_service()
    #     self.timer.stop()
    #     return 0

    def loop(self):
        if self.status == SERVICE_STATUS.STARTED:
            current_time = time.time()
            if current_time - self.last_update > (1 / self.fps):
                self.step()
                self.last_update = current_time
                # logging.debug('Time taken per step: {}'.format(time.time() - current_time))

    def step(self):
        raise NotImplementedError

