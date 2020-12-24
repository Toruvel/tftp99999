#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <unistd.h>

#define MAXWAIT 100
#define PROCWAIT 0
#define MAXCLIENTS 200


//задача написать программу, которая принимает и отправляет пакеты (tftp rfc1350)#int maxsoc = 10;
//pollfd (?)

struct client{
       int soc;
       int process;
       FILE* file;
       int pack;
       struct client *next;
    };

struct client_list {
    struct client *head;
    unsigned int num;
};

struct client* add_client(struct client_list *clients, int soc){
    struct client *temp;
    temp = (struct client*)malloc(sizeof(struct client));
    temp->soc = soc;
    temp->process = 0;
    temp->file = NULL;
    temp->pack = 0;
    temp->next = clients->head;
    clients->head = temp;
    clients->num++;
    return(temp);
}

struct client * delete_client(struct client_list *clients, struct client *cl) {
    struct client *temp;
    if (cl == clients->head) {
        clients->head = cl->next;
    } else {
        temp = clients->head;
        while (temp->next != cl) {
            temp = temp->next;
        }
        temp->next = cl->next;
    }
    free(cl); 
    clients->num--;
    return(temp);
}

void tftp_error(int errcode){
    
}

int readsoc (struct client *cl){ //read from socket
    unsigned char buf[516];
    int tecsoc = cl->soc;
    int rez;
    int len;
    uint16_t *p;
    uint16_t *optr;
    uint16_t opcode;
    char* file_name;
    char* mode;
    
    
   if ((rez = recv (tecsoc, &buf, sizeof(buf),0))== -1) {
          perror("server: bind()");//cлучай когда ничего не считал(?)
          close(tecsoc);
          exit(1); //могу?  (нужна функция удаления клиента из списка)
   };
   optr = (uint16_t *)buf;
   opcode = ntohs(*optr);
   //если запрос на чтение
   if (opcode == 1 ) { 
       if (rez==516){  // слишком большой пакет
           tftp_error(4);
          return(-1);
       }
       if (cl->process != 0) {  // предыдущая операция не закончена
           tftp_error(4);
           //тут должен быть выход
          return(-1);
       }

       if (buf[rez-1] != 0) {  // последний байт должен быть 0
           tftp_error(4);
           return(-1);
       }
       file_name = (char *)(&buf[2]);
       len = strlen(file_name);
       if (len + 3 >= rez) {  // file_name идёт до конца, mode отсутствует
           tftp_error(4);
           return(-1);
       }
       mode = (char *)(&buf[len + 3]);
       if ((strncasecmp("octet", mode, 6) != 0) && (strncasecmp("netascii", mode, 9) != 0)) {  // неправильный mode
           tftp_error(4);
           return(-1);
       }
       cl->file = fopen(file_name, "r");
       if (cl->file == -1) {
           tftp_error(1);
           return(-1);
       }
       p = (uint16_t *)buf;
       *p = htons(3);  // DATA packet
       ++p;
       cl->pack = 1;
       *p = htons(cl->pack);
       ++p;
       len = fread(p, 1, sizeof(512), cl->file);
       if ((rez = send(cl->soc, buf, 4 + len, 0)) < 0) {
           perror("server: sendto()");
           tftp_error(4);
           return(-1);
       }
       cl->process = opcode;
     }
   }
   //запрос на записть
       
    if (opcode == 2){ 
       if (cl->process != 0){
           tftp_error(4);
           return(-1);
       }
       if (rez==516){  // слишком большой пакет
           tftp_error(4);
          return(-1);
       }
       if (cl->process != 0) {  // предыдущая операция не закончена
           tftp_error(4);
           //тут должен быть выход
          return(-1);
       }

       if (buf[rez-1] != 0) {  // последний байт должен быть 0
           tftp_error(4);
           return(-1);
       }
       file_name = (char *)(&buf[2]);
       len = strlen(file_name);
       if (len + 3 >= rez) {  // file_name идёт до конца, mode отсутствует
           tftp_error(4);
           return(-1);
       }
       mode = (char *)(&buf[len + 3]);
       if ((strncasecmp("octet", mode, 6) != 0) && (strncasecmp("netascii", mode, 9) != 0)) {  // неправильный mode
           tftp_error(4);
           return(-1);
       }
       cl->file = fopen(file_name, "w");
       if (cl->file == -1) {
           tftp_error(1);
           return(-1);
       }
       p = (uint16_t *)buf;
       *p = htons(4);  // ACK packet
       ++p;
       cl->pack = 0;
       *p = htons(cl->pack);
       if ((rez = send(cl->soc, buf, 4, 0)) < 0) {
           perror("server: sendto()");
           tftp_error(4);
           return(-1);
       }
       cl->process = opcode;
     }
    }
    
    if (opcode == 3) { 
       if (cl->process != 2){
           tftp_error(4);
           return(-1);
       }
     }   
    
    if (opcode == 4) { 
       if (cl->process != 1){
           tftp_error(4);
           return(-1);
       }
     }   
    
    if (opcode == 5) {
        fprintf(stderr, "Error packet: ErrorCode=%d %s", ntohs(*((uint16_t *)(buf+2))), (char *)(buf + 4));
        return(-1);
     }   
  return(0); 
}

int main(int argc, char* argv[ ])
{
   int sfirst;
   int snext;
   int timeout = 240000;
   int rezpoll;
   int i;
   int socrecv;
   
   int flags = 0;
   //int razmersoc1;
   uint16_t port = 0;
   struct sockaddr_in sockaddr1;
    struct client_list clients;
    struct client *cltec=NULL;
    struct client *clnext=NULL;
    struct pollfd sockmas[MAXCLIENTS+1];
//    struct client *clfind[RAZM];
   
     if (argc > 2) {
          if (sscanf(argv[2], "%hu", &port)) {
               port = htons(port);
          } else {
               fprintf(stderr, "error: invalid port number\n");
               exit(1);
          }
     }
    //poll https://www.opennet.ru/cgi-bin/opennet/man.cgi?topic=poll&category=2

    //accept

    //открыть сокет на котором будет ждать запроса
    
   sfirst = socket(AF_INET, SOCK_STREAM, 0);//SOCK_DGRAM
//   printf("sfirst=%d\n", sfirst);
   if( sfirst == -1){
        perror("SERVER: recvfrom");
		exit(1);
   }   
    
   sockaddr1.sin_family = AF_INET;
   sockaddr1.sin_addr.s_addr = htonl(INADDR_ANY);
   sockaddr1.sin_port = port; 
   //bind связывает сокет с конкретным адресом
    if (bind(sfirst, (struct sockaddr *) &sockaddr1, sizeof(sockaddr1)) == -1) {
          perror("server: bind()");
          close(sfirst);
          exit(1);
     }

    if ((flags = fcntl(sfirst, F_GETFL, 0)) == -1)
        flags = 0;
    fcntl(sfirst, F_SETFL, flags | O_NONBLOCK);
   printf("tftp server: listening on %d\n", ntohs(sockaddr1.sin_port));
   
   
    
   listen (sfirst, MAXWAIT);
   

   sockmas[0].fd = sfirst; //sockmas[0] первый сокет
   sockmas[0].events = POLLIN;
   sockmas[0].revents = 0;
    


   //цикл для poll
    do{
        cltec = clients.head;// присваиваем текущему значению указатель на начало списка
    
        i = 1;
        while ((cltec!=NULL)) {
            sockmas[i].fd = cltec->soc; //тут должен быть номер сокета
            sockmas[i].events = POLLIN;
            ++i;
            cltec = cltec->next;
        }

        rezpoll = poll(sockmas, clients.num + 1, timeout); 
    
   if (rezpoll == -1) {
       perror("server: poll()");
       exit(1);
   }
   else if (rezpoll == 0){
       perror("poll timeout");
       exit(1);
   }
   else {
       if (sockmas[0].revents & POLLIN){
           sockmas[0].revents = 0;  
           //добавлять условие про количество клиентов
           
           if ((snext = accept(sfirst, NULL, NULL))==-1){
               perror("server: accept()");
               exit(1);
           } else {
               if (clients.num >= MAXCLIENTS) {
                 perror("Too many clients");
                 close(snext);
               } else {
                   if ((flags = fcntl(snext, F_GETFL, 0)) == -1)
                       flags = 0;
                   fcntl(snext, F_SETFL, flags | O_NONBLOCK);

                   add_client(&clients, snext);
               }
            }
           }
        }
        cltec = clients.head;
        for (int j=1; j <= clients.num; ++j){
            clnext = cltec->next;
            if (sockmas[j].revents & POLLIN){
                sockmas[j].revents = 0;

                if (readsoc(cltec) == -1)
                    delete_client(&clients, cltec);
                
            }
            cltec = clnext;
        }
} while (rezpoll > 0);
   
   
      
          

}
