#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>



#define WAIT_SMALL 1000
#define WAIT_BIG   60000

#define PROCWAIT 0
#define MAXCLIENTS 200

//задача написать программу, которая принимает и отправляет пакеты (tftp rfc1350)


struct client{
       int soc;
       struct sockaddr addr;
       socklen_t addr_len;
       int process;
       int len;
       FILE* file;
       int pack;
       unsigned char buf[516];
       time_t sent;
       struct client *next;
    };

struct client_list {
    struct client *head;
    unsigned int num;
};

struct client* add_client(struct client_list *clients, int soc, uint16_t opcode, FILE* file, struct sockaddr *addr, socklen_t addr_len){
    struct client *temp;
    temp = (struct client*)malloc(sizeof(struct client));
    temp->soc = soc;
    temp->addr_len = addr_len;
    memset(&temp->addr, 0, sizeof(temp->addr));
    memcpy(&temp->addr, addr, addr_len);
    temp->process = opcode;
    temp->file = file;
    temp->pack = 0;
    temp->len = 0;
    temp->sent = 0;
    temp->next = clients->head;
    clients->head = temp;
    clients->num++;
    fprintf(stderr, "Added new client. %d clients active\n", clients->num);
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
    fprintf(stderr, "Deleted client. %d clients active\n", clients->num);
    return(temp);
}

int sendto_client(struct client *cl, unsigned char *buf, int len) {
    int rez;
    uint16_t opcode;

    opcode = ntohs(*(uint16_t *)buf);
    switch (opcode) {
        case 1:
            fprintf(stderr, "Sending %d bytes RRQ\n", len);
            break;
        case 2:
            fprintf(stderr, "Sending %d bytes WRQ\n", len);
            break;
        case 3:
            fprintf(stderr, "Sending %d bytes DATA #%d\n", len, cl->pack);
            break;
        case 4:
            fprintf(stderr, "Sending %d bytes ACK #%d\n", len, cl->pack);
            break;
        case 5:
            fprintf(stderr, "Sending %d bytes ERROR after %d blocks\n", len, cl->pack);
            break;
    }
    
    if ((rez = sendto(cl->soc, buf, len, 0, &(cl->addr), cl->addr_len)) < 0) {
        perror("server: sendto()");
        return(-1);
    }
    return 0;
}

char error_messages[8][36] = {
    "Not defined",
    "File not found.",
    "Access violation.",
    "Disk full or allocation exceeded.",
    "Illegal TFTP operation.",
    "Uncnown transfer ID.",
    "File already exist",
    "No such user."
};

void error(int errcode, struct client *cl){//поменять вызов функции
    unsigned char buf[516];
//    char *kb;
    char *nm;
//    int rez;
    uint16_t *p;
    int len;

    p = (uint16_t *)buf;
    *p = htons(5);  
    ++p;
    *p = htons(errcode);
    ++p;
    if (errcode >= 0 && errcode < 8)
        nm = error_messages[errcode];
    else
        nm = error_messages[0];//тут надо переписать функцию так, чтобы она передавала еще и сообщение об ошибке, если та нетипичная
    len = strlen(nm) + 1;
    strncpy((char *)p, nm, len);  
    sendto_client(cl, buf, len+ 4);
};

int rrq(struct client *cl) { // запрос на чтение
    int rez;
    int len;
    uint16_t *p;
    struct timeval time;

    p = (uint16_t *)(cl->buf);
    *p = htons(3);  // DATA packet
    ++p;
    cl->pack = 1;
    *p = htons(cl->pack);
    ++p;
    len = fread(p, 1, 512, cl->file);
    cl->len = 4+len;
    if (len < 512) {  // файл закончился
        fclose(cl->file);
        cl->file = NULL;
    }
    if ((rez = sendto_client(cl, cl->buf, cl->len)) < 0) {
        return(-1);
    }
    if (gettimeofday(&time, NULL)) {
        perror("server: gettimeofday");
		exit(1);
    }
    cl->sent = time.tv_sec;
    return 0;
}

int wrq(struct client *cl) { // запрос на запись
    int rez;
    int len;
    uint16_t *p;
    struct timeval time;

    p = (uint16_t *)(cl->buf);
    *p = htons(4);  // ACK packet
    ++p;
    cl->pack = 0;
    *p = htons(cl->pack);
    cl->len = 4;
    if (sendto_client(cl, cl->buf, cl->len) < 0) {
        return(-1);
    }
    if (gettimeofday(&time, NULL)) {
        perror("server: gettimeofday");
		exit(1);
    }
    cl->sent = time.tv_sec;
       
    return 0;
}

int data(struct client *cl, unsigned char *buf, int len) { // кусок файла
    uint16_t *p;
    uint16_t block;
    int flen;
    struct timeval time;

    if (cl->process != 2) {
        error(4,cl);
        return(-1);
    }

    p = (uint16_t *)(buf+2);
    block = ntohs(*p);
    if (block != cl->pack + 1) {

        // ничего не делаем, ждём другой кусок
        return 0;
    }

    flen = fwrite(buf+4, 1, len - 4, cl->file);
    cl->pack = block;

    p = (uint16_t *)(cl->buf);
    *p = htons(4);  // ACK packet
    ++p;
    *p = htons(cl->pack);
    cl->len = 4;
    if (sendto_client(cl, cl->buf, cl->len) < 0) {
        return(-1);
    }
    if (gettimeofday(&time, NULL)) {
        perror("server: gettimeofday");
		exit(1);
    }
    cl->sent = time.tv_sec;

    if (len < 516) {  // неполный пакет - файл закончился
        fclose(cl->file);
        close(cl->soc);
        cl->file = NULL;
        cl->soc = 0;
        cl->process = 0;
        fprintf(stderr, "Complete WRQ of %d blocks\n", cl->pack);
        return -1;
    }
    return 0;
}

int ack(struct client *cl, unsigned char *buf, int len) { // подтверждение
    uint16_t *p;
    uint16_t block;
    int flen;
    struct timeval time;

    if (cl->process != 1){
        error(4,cl);
        return(-1);
    }

    p = (uint16_t *)(buf+2);
    block = ntohs(*p);
    if (block != cl->pack) {
        // ничего не делаем, ждём другое подтверждение
        return 0;
    }

    if (cl->file == NULL) {  // файл уже закончился, мы получили подтверждение приёма
        close(cl->soc);
        cl->soc = 0;
        cl->process = 0;
        fprintf(stderr, "Complete RRQ of %d blocks\n", cl->pack);
        return -1;
    }

    ++(cl->pack);
    p = (uint16_t *)(cl->buf);
    *p = htons(3);  // DATA пакет
    ++p;
    *p = htons(cl->pack);
    ++p;
    flen = fread(p, 1, 512, cl->file);
    fprintf(stderr, "Read %d bytes\n", flen);
    cl->len = 4+flen;
    if (flen < 512) {  // файл закончился
        fclose(cl->file);
        cl->file = NULL;
    }
    if (sendto_client(cl, cl->buf, cl->len) < 0) {
        return(-1);
    }
    if (gettimeofday(&time, NULL)) {
        perror("server: gettimeofday");
		exit(1);
    }
    cl->sent = time.tv_sec;
       
    return 0;
}

int readsoc(struct client *cl){ //read from socket
    unsigned char buf[516];
    int rez;
    int len;
    uint16_t *p;
    uint16_t *optr;
    uint16_t opcode;
    uint16_t block;
    char* file_name;
    char* mode;
    
    
   if ((rez = recv(cl->soc, &buf, sizeof(buf),0))== -1) {
          perror("server: recv()");
          close(cl->soc);
          exit(1);  
   };
   optr = (uint16_t *)buf;
   opcode = ntohs(*optr);
    switch (opcode) {
        case 1:
            fprintf(stderr, "Received %d bytes RRQ\n", rez);
            break;
        case 2:
            fprintf(stderr, "Received %d bytes WRQ\n", rez);
            break;
        case 3:
            fprintf(stderr, "Received %d bytes DATA #%d\n", rez, ntohs(optr[1]));
            break;
        case 4:
            fprintf(stderr, "Received %d bytes ACK #%d\n", rez, ntohs(optr[1]));
            break;
        case 5:
            fprintf(stderr, "Received %d bytes ERROR after %d blocks\n", rez, cl->pack);
            break;
    }

   //если запрос на чтение
   if (opcode == 1 ) { 
        error(4,cl);
        return(-1);
   }

   //запрос на запись
    if (opcode == 2){ 
        error(4,cl);
        return(-1);
    }

    // кусок файла
    if (opcode == 3) {
        return data(cl, buf, rez);
     }   

    // подтверждение
    if (opcode == 4) {
        return ack(cl, buf, rez);
     }   

    // ошибка
    if (opcode == 5) {
        fprintf(stderr, "Error packet: ErrorCode=%d %s\n", ntohs(*((uint16_t *)(buf+2))), (char *)(buf + 4));
        return(-1);
     }   
  return(0); 
}

int resend_client(struct client *cl) { // посылаем ещё раз последний пакет
    struct timeval time;

    if (cl->process != 0){
        if (sendto_client(cl, cl->buf, cl->len) < 0) {
            return(-1);
        }
        if (gettimeofday(&time, NULL)) {
            perror("server: gettimeofday");
            exit(1);
        }
        cl->sent = time.tv_sec;
    }
       
    return 0;
}

struct client* process_first_packet(struct client_list *clients, struct sockaddr *from, socklen_t fromLen, struct addrinfo *addr, unsigned char *buf, int len) {
    uint16_t opcode;
    char *file_name, *mode;
    int flen;
    int flags;
    FILE* file;
    int s;

    opcode = ntohs(*(uint16_t *)buf);
    fprintf(stderr, "First packet opcode=%d\n", opcode);
    if (opcode != 1 && opcode != 2) {
        perror("Invalid packet");
        return NULL;
    }

    if (len==516) {
        perror("Too large packet");
        return NULL;
    }

    if (buf[len-1] != 0) {  // последний байт должен быть 0
        perror("Invalid packet");
        return NULL;
    }

    file_name = (char *)(&buf[2]);
    flen = strlen(file_name);
    if (flen + 3 >= len) {  // file_name идёт до конца, mode отсутствует
        perror("Invalid file name");
        return NULL;
    }

    mode = (char *)(&buf[flen + 3]);
    fprintf(stderr, "file \"%s\", mode %s\n", file_name, mode);
    if (strncasecmp("octet", mode, 6) != 0) {  
        perror("Invalid mode");
        return NULL;
    }


    if (clients->num >= MAXCLIENTS) {
        perror("Too many clients");
        return NULL;
    }

    if ((s = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol)) == -1) {
        perror("server: socket");
        return NULL;
    }


    if ((flags = fcntl(s, F_GETFL, 0)) == -1)
        flags = 0;
    fcntl(s, F_SETFL, flags | O_NONBLOCK);

    if (opcode == 1)
        file = fopen(file_name, "r");
    else
        file = fopen(file_name, "w");
    if (file == NULL) {
        perror("server: fopen");
        close(s);
        return NULL;
    }

    return add_client(clients, s, opcode, file, from, fromLen);
}

int main(int argc, char* argv[ ])
{
   struct addrinfo hints, *addr;
   char * service = "tftp";
   int sfirst;
   int timeout;
   int rezpoll;
   int i;
   int socrecv;
   int secsend;
   unsigned char buf[516];
   int len;
   struct sockaddr from;
   socklen_t fromLen;
   
   int flags = 0;

   struct sockaddr_in *sockaddr1;
    struct client_list clients;
    struct client *cltec=NULL;
    struct client *clnext=NULL;
    struct client *clnew=NULL;
    struct pollfd sockmas[MAXCLIENTS+1];
    struct timeval time;

    if (argc > 1) {
        service = argv[1];
     } else {
        service = "tftp";
     }


    memset(&hints, 0, sizeof (hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_ADDRCONFIG | AI_PASSIVE;

    if (getaddrinfo(NULL, service, &hints, &addr)) {
        perror("server: getaddrinfo");
		exit(1);
    }
    sockaddr1 = (struct sockaddr_in *) (addr->ai_addr);

    //открыть сокет на котором будет ждать запроса
    
   sfirst = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);

   if( sfirst == -1){
        perror("server: socket");
		exit(1);
   }   

    if (bind(sfirst, addr->ai_addr, addr->ai_addrlen) == -1) {
          perror("server: bind()");
          close(sfirst);
          exit(1);
     }

    if ((flags = fcntl(sfirst, F_GETFL, 0)) == -1)
        flags = 0;
    fcntl(sfirst, F_SETFL, flags | O_NONBLOCK);
   printf("tftp server: listening on %d\n", ntohs(sockaddr1->sin_port));
   
   

   sockmas[0].fd = sfirst; // первый сокет
   sockmas[0].events = POLLIN;
   sockmas[0].revents = 0;
    
   clients.head = NULL;
   clients.num = 0;

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

        if (clients.num == 0)
            timeout = WAIT_BIG;
        else
            timeout = WAIT_SMALL;
        rezpoll = poll(sockmas, clients.num + 1, timeout); 
    
   if (rezpoll == -1) {
       perror("server: poll()");
       exit(1);
   }

   if (gettimeofday(&time, NULL)) {
       perror("server: gettimeofday");
       exit(1);
   }

   if (rezpoll == 0){

       if (clients.num == 0) {
           perror("poll timeout");
           exit(1);
       } else {
           cltec = clients.head;// присваиваем текущему значению указатель на начало списка
           while ((cltec!=NULL)) {
               resend_client(cltec);
               cltec = cltec->next;
           }
       }
   }
   else {
       if (sockmas[0].revents & POLLIN){
           sockmas[0].revents = 0;  

           fromLen = sizeof(from);
           
           len = recvfrom(sfirst, buf, sizeof(buf), 0, (struct sockaddr*)&from, &fromLen);

           if (fromLen > sizeof(from)) {
               fprintf(stderr, "recvfrom(): returned invalid address size %u instead of %lu\n", fromLen, sizeof(from));
               exit(1);
           }
           if (len <= 4) {
               fprintf(stderr, "Received invalid packet size %d\n", len);

           } else {
               clnew = process_first_packet(&clients, (struct sockaddr*)&from, fromLen, addr, buf, len);
               if (clnew != NULL) {
                   if (clnew->process == 1)
                       rrq(clnew);
                   else 
                       wrq(clnew);
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
            else {
                if ((time.tv_sec - cltec->sent) * 1000 > WAIT_SMALL)
                    resend_client(cltec);
            }
            cltec = clnext;
        }
   }
} while (rezpoll > 0);
   
   
      
          

}