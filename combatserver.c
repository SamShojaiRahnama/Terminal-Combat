
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>   // added for randomizing attack and hp values

#ifndef PORT
#define PORT 57359
#endif

# define SECONDS 10


/* (new struct)
* *curr_opponent points to the opponent they face
* past_fd = fd of the client who they faced last

* progress_game 1 or 0 to tell if player in a match or not
* pmoves for powermoves and hp for health
*/
struct fight {
    int prop_hp;
    int checking_turns;
    struct client *curr_opponent;
    int past_fd;
    int progress_game; //0 or 1
    int hp;
    int pmoves;
};


struct msg_handling {
    char buf_message[256]; //messages buffer
    int space; // space left for messages
    char *next; //  pointer to available position for msg
    int check_mbuf; // max index
};

struct inp_handling {
    char buff_inp[256]; // input action buffer
    int inp_space; // space for writing actions
    char *inp_next; // pointer to available position
    int check_inpbuff; // max index
};


struct client {
    struct in_addr ipaddr;
    struct client *next;
    struct msg_handling direct_handle; // message handling
    struct inp_handling inp_handle; //handling inputs actions
    struct fight clash;
    int fd;
    int check;
    int speak; // 1 if client spoke and 0 otherwise / basically interacted with terminal
    int status; // 1 if client's turn and 0 if not
    char name[64]; // Limited to size of 16 chars for name
};


static struct client *addclient(struct client *top, int fd, struct in_addr addr);

static struct client *removeclient(struct client *top, int fd);

static void broadcast(struct client *top, char *s, int size);

int handleclient(struct client *p, struct client *top);

int filter_inp(struct client *p); //checks if name set and if it turn of client to speak/ and not allow to speak

int opp_connector(struct client *head, struct client *p); //finding opponent after game

int information(struct client *p1, struct client *p2);

int player_choices(struct client *p1, struct client *p2); //displays options for each turn for the client

int convert_inp(struct client *head, struct client *p); //converts the input based on the options a,p,h,s to action

void revert_choicebf(struct client *p);

void fixing_msgbf(struct client *p) ;

void losing_situation(struct client *p1, struct client *p2);

void messaging_against(struct client *p);



int bindandlisten(void);

int main(void) {
    printf("player deployed to port %d\n", PORT);
    int clientfd, maxfd, nready;
    struct client *p;
    struct client *head = NULL;
    socklen_t len;
    struct sockaddr_in q;
    struct timeval tv;
    fd_set allset;
    fd_set rset;

    int i;


    int listenfd = bindandlisten();
    // initialize allset and add listenfd to the
    // set of file descriptors passed into select
    FD_ZERO(&allset);
    FD_SET(listenfd, &allset);
    // maxfd identifies how far into the set to search
    maxfd = listenfd;

    while (1) {
        // make a copy of the set before we pass it into select
        rset = allset;
        nready = select(maxfd + 1, &rset, NULL, NULL, &tv);


        if (nready == -1) {
            perror("select");
            continue;
        }

        if (FD_ISSET(listenfd, &rset)){
            printf("a new client is connecting\n");
            len = sizeof(q);
            if ((clientfd = accept(listenfd, (struct sockaddr *)&q, &len)) < 0) {
                perror("accept");
                exit(1);
            }

            FD_SET(clientfd, &allset);
            if (clientfd > maxfd) {
                maxfd = clientfd;
            }
            printf("connection from %s\n", inet_ntoa(q.sin_addr));
            head = addclient(head, clientfd, q.sin_addr);
        }

        for(i = 0; i <= maxfd; i++) {
            if (FD_ISSET(i, &rset)) {
                for (p = head; p != NULL; p = p->next) {
                    if (p->fd == i) {
                        int result = handleclient(p, head);
                        if (result == -1) {
                            int tmp_fd = p->fd;
                            head = removeclient(head, p->fd);
                            FD_CLR(tmp_fd, &allset);
                            close(tmp_fd);
                        }
                        break;
                    }
                }
            }
        }
    }
    return 0;
}



int handleclient(struct client *p, struct client *top) {
    char final[256]; // checking if the player is in match for the turn
    if (p->status == 1) {
        if ((p->clash).progress_game != 0) {
            return convert_inp(top, p);
        }
    }
    int result = filter_inp(p); // something has been types in regards to name or message

    if (p->speak != 0) {
        if (result < 2) {
            p->check = 0;
        } else{
            p->speak = 0; // if message send it and change state accordingly
        }
        return result;
    }

    if (result == -1) { // client has disconected as the result of negative indicates failure

        printf("Disconnect from %s\n", inet_ntoa(p->ipaddr));
        sprintf(final, "Bye Bye %s\r\n", inet_ntoa(p->ipaddr));
        broadcast(top, final, strlen(final));
        return result;
    }

    if (result == 1) {// oppourutnity for setting name
        // Set name
        sprintf(final, "waiting for opponent\r\n");
        write(p->fd, final, strlen(final) + 1);
        sprintf(final, "%s joined the server\r\n", p->name);
        broadcast(top, final, strlen(final) + 1);

        int againt_p = opp_connector(top, p);
        if (againt_p != 0 && (p->clash.progress_game == 0 || p->clash.progress_game == 1)) {
            player_choices(p, (p->clash).curr_opponent);
            information(p, (p->clash).curr_opponent);
        }

        return 1;
    }

    if (p->status == 0) { // not the players turn and types something
        if (p->name[0] != '\0' && (p->clash).progress_game != 0) {
            read(p->fd, (p->direct_handle).next, (p->direct_handle).space);
            fixing_msgbf(p); // we filter their message by removing and altering buffer
            return 0;
        }
    }
    return 0;
}


/* bind and listen, abort on error
 * returns FD of listening socket
 */
int bindandlisten(void) {
    struct sockaddr_in r;
    int listenfd;

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }
    int yes = 1;
    if ((setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int))) == -1) {
        perror("setsockopt");
    }
    memset(&r, '\0', sizeof(r));
    r.sin_family = AF_INET;
    r.sin_addr.s_addr = INADDR_ANY;
    r.sin_port = htons(PORT);

    if (bind(listenfd, (struct sockaddr *)&r, sizeof r)) {
        perror("bind");
        exit(1);
    }

    if (listen(listenfd, 5)) {
        perror("listen");
        exit(1);
    }
    return listenfd;
}

void initialize_players(struct client *p, int fd, struct in_addr addr) {

    //initializing client
    p->speak = 0;
    p->status = 0;
    p->check = 0;
    p->fd = fd;
    p->name[0] = '\0';
    p->ipaddr = addr;
    p->next = NULL;

    // Initializing buffer
    fixing_msgbf(p);
    revert_choicebf(p);

    // Initializing clash actions
    p->clash.prop_hp = 0;
    p->clash.checking_turns = 0;
    p->clash.progress_game = 0;
    p->clash.hp = 0;
    p->clash.pmoves = 0;
    p->clash.curr_opponent = NULL;
    p->clash.past_fd = fd;
}


static struct client *addclient(struct client *top, int fd, struct in_addr addr) {
    struct client *p = malloc(sizeof(struct client));
    char output[256];

    if (!p) {
        perror("malloc");
        exit(1);
    }

    // helper to help initialize player
    initialize_players(p, fd, addr);

    // Alert player to insert name
    sprintf(output, "Insert name: ");
    write(fd, output, strlen(output) + 1);

    // Add new client to end of linked list if not empty
    if (top != NULL) {
        struct client *iterator = top;
        while (iterator->next != NULL) {
            iterator = iterator->next;
        }
        iterator->next = p;
    } else {
        top = p;
    }

    printf("Adding client %s\n", inet_ntoa(addr));
    return top;
}



static struct client *removeclient(struct client *top, int fd) {
    struct client **p;

    for (p = &top; *p && (*p)->fd != fd; p = &(*p)->next)
        ;
    // Now, p points to (1) top, or (2) a pointer to another client
    // This avoids a special case for removing the head of the list
    if (*p) {
        //alterations made here

        char total[256];
        if (((*p)->clash).progress_game != 0) { //this client wants to leave in the middle of a game

            struct client *chosen_opp = (*p)->clash.curr_opponent;

            sprintf(total, "\n%s has bowed down, you win automatically\r\n", (*p)->name);
            write(chosen_opp->fd, total, strlen(total) + 1);

            // Reset the opponent's game state

            if (chosen_opp->status != 0) {
                chosen_opp->status = 0;
            }

            if (chosen_opp->clash.progress_game != 0) {
                chosen_opp->clash.progress_game = 0;
            }

            if (chosen_opp->clash.past_fd != 0) {
                chosen_opp->clash.past_fd = 0;
            }

            // Notify the opponent to wait for the next game
            sprintf(total, "\n*waiting for next opponent*\r\n");
            write(chosen_opp->fd, total, strlen(total) + 1);


            // Attempt to find a new opponent for the player's opponent
            int opponent = opp_connector(top, chosen_opp);
            if (opponent != 0) {
                if (chosen_opp->clash.progress_game == 1 || chosen_opp->clash.progress_game == 0){
                    player_choices(chosen_opp, chosen_opp->clash.curr_opponent);
                    information(chosen_opp, chosen_opp->clash.curr_opponent);
                }

            }

            sprintf(total, "\n%s left\r\n", (*p)->name);
            broadcast(top, total, strlen(total) + 1);


            struct client *t = (*p)->next;
            printf("Removing client %d %s\n", fd, inet_ntoa((*p)->ipaddr));
            free(*p);
            *p = t;
        }
    } else {
        fprintf(stderr, "Trying to remove fd %d, but I don't know about it\n",
                fd);
    }
    return top;
}


static void broadcast(struct client *top, char *s, int size) {
    struct client *p;
    for (p = top; p; p = p->next) {
        if (write(p->fd, s, size) == -1) {
            perror("write");
        }
    }
    /* should probably check write() return value and perhaps remove client */
}



//======================Additonal Function Stated Below======================//

int filter_inp(struct client *p) {
    int returnval = 0;
    int total_byt;

    total_byt = read(p->fd, p->direct_handle.next, p->direct_handle.space); //reading from client


    if (total_byt == 0) {
        return 0;
    } else if (total_byt < 0) {
        perror("issue reading");
        returnval =  -1;
    }

    p->direct_handle.check_mbuf = p->direct_handle.check_mbuf + total_byt; //get new updated total bytes in buffer

    int place = -1;
    int term = 0;
    int conditional = 0;

// iterating over message buffer
    while (term < p->direct_handle.check_mbuf && conditional == 0) {
        if (p->direct_handle.buf_message[term] == '\r' || p->direct_handle.buf_message[term] == '\n') {
            place = term; //getting newline
            conditional = 5; //exiting loop here
        }
        term = term + 1;
    }

    if (!(place < 0)) {
        p->direct_handle.buf_message[place] = '\0';
        if (p->check == 0 && p->name[0] == '\0') { //first message for setting name as they havnt already
            strncpy(p->name, p->direct_handle.buf_message, sizeof(p->name) - 1);
            p->name[sizeof(p->name) - 1] = '\0'; //making sure that the last one is null terminated
            p->direct_handle.buf_message[place] = '\0';
            returnval = 1;

        } else if (p->speak == 1) { // Player already set name, this message has to be for speaking
            messaging_against(p);
            returnval = 2;
        }

        if (p->clash.checking_turns == 0){
            int add = (place + 2);
            p->direct_handle.check_mbuf = p->direct_handle.check_mbuf - add;
            memmove(p->direct_handle.buf_message, p->direct_handle.buf_message + add, sizeof(p->direct_handle.buf_message));
        }
        //copies 256-(position+2) bytes, starting from (position+2) from the source buffer (msg_buffer) to the destination buffer (msg_buffer)
        //essentially works as if im shifting it (POTENTIAL ERROR: might lose position+2 bytes of data but use that as a concern in the vid)

    }
    p->direct_handle.space = sizeof(p->direct_handle.buf_message) - p->direct_handle.check_mbuf; // Update remaining space
    p->direct_handle.next = p->direct_handle.buf_message + p->direct_handle.check_mbuf;

    return returnval;
}



void messaging_against(struct client *p) {
    //get players name with says with message with newline at end
    char final[320];
    strcpy(final, p->name);
    strcat(final, " says: ");
    strcat(final, p->direct_handle.buf_message);
    strcat(final, "\n");


    // Write the message to the opponent's file descriptor directly
    write(p->clash.curr_opponent->fd, final, strlen(final) + 1);

    // Reset speak flag after message is sent
    p->speak = 0;
}


int opp_connector(struct client *head, struct client *player) {
    char final[256];
    struct client *chosenClient = head;
    int getHP, getPm;

    srand(time(NULL));
    while (chosenClient != NULL) {

        int matching = (chosenClient->clash).progress_game;
        char clientname = (chosenClient->name)[0];
        char playername = (player->name)[0];
        int checkingclient = chosenClient->fd;
        int previousclientcheck = (chosenClient->clash).past_fd;
        int previousplayercheck = (player->clash).past_fd;

        // Use nested if conditions
        if (matching != 1 && clientname != '\0' && playername != '\0') {
            if (checkingclient != player->fd && previousclientcheck != player->fd && previousplayercheck != checkingclient) {
                getHP = (rand() % 11) + 20;  // HP between 20 and 30
                getPm = (rand() % 3) + 1;  // Power moves between 1 and 3

                if (chosenClient->clash.progress_game != 1) {
                    chosenClient->clash.progress_game = 1;
                }

                if (chosenClient->clash.curr_opponent != player) {
                    chosenClient->clash.curr_opponent = player;
                }

                if (chosenClient->clash.past_fd != player->fd) {
                    chosenClient->clash.past_fd = player->fd;
                }
                chosenClient->clash.hp = getHP;
                chosenClient->clash.pmoves = getPm;



                if (player->clash.progress_game != 1) {
                    player->clash.progress_game = 1;
                }

                if (player->clash.curr_opponent != chosenClient) {
                    player->clash.curr_opponent = chosenClient;
                }

                if (player->clash.past_fd != chosenClient->fd) {
                    player->clash.past_fd = chosenClient->fd;
                }
                player->clash.hp = getHP;
                player->clash.pmoves = getPm;

                int start = rand() % 2;
                if (start == 0) {
                    player->status = 1;
                    chosenClient->status = 0;
                } else {
                    chosenClient->status = 1;
                    player->status = 0;
                }


                snprintf(final, sizeof(final), "\nYour opponent is %s\r\n", chosenClient->name);
                write(player->fd, final, strlen(final) + 1);

                snprintf(final, sizeof(final), "\nYour opponent is %s\r\n", player->name);
                write(chosenClient->fd, final, strlen(final) + 1);

                return 1;
            }
        }


        chosenClient = chosenClient->next;
    }
    return 0;
}







int information(struct client *first_p, struct client *sec_p) {
    char update_first[512];
    char update_second[512]; // having the amount of char for the current update for each player

    // given update for the first player
    snprintf(update_first, sizeof(update_first),
             "Your current hp: %d\nYour powermove count: %d\n%s's current hp: %d\n",
             first_p->clash.hp, first_p->clash.pmoves, sec_p->name, sec_p->clash.hp);

    // given update for the second player
    snprintf(update_second, sizeof(update_second),
             "Your current hp: %d\nYour powermove count: %d\n%s's current hp: %d\n",
             sec_p->clash.hp, sec_p->clash.pmoves, first_p->name, first_p->clash.hp);


    write(first_p->fd, update_first, strlen(update_first) + 1);
    write(sec_p->fd, update_second, strlen(update_second) + 1);

    return 0;
}



int player_choices(struct client *first_p, struct client *sec_p) {
    char max[256]; // have maximum output here

    if (first_p->status == 0 && sec_p->status == 1) { // making sure its second players turn

        if ((sec_p->clash).pmoves <= 0) { // giving correct output if no powermoves exist
            sprintf(max, "\n(a)ttack, (h)pswap, (s)peak\r\n");
        } else {
            sprintf(max, "\n(a)ttack, (p)owermove, (h)pswap, (s)peak\r\n");
        }
        write(sec_p->fd, max, strlen(max) + 1); // providing those choices

        // alter first player about the second player's turn
        sprintf(max, "\nwaiting for %s to make a move\r\n", sec_p->name);
        write(first_p->fd, max, strlen(max) + 1);
    } else { // If it's p1's turn
        // Handle p1's options
        if ((first_p->clash).pmoves <= 0) { // giving correct output if no powermoves exist
            sprintf(max, "\n(a)ttack, (h)pswap, (s)peak\r\n");
        } else { //
            sprintf(max, "\n(a)ttack, (p)owermove, (h)pswap, (s)peak\r\n");
        }
        write(first_p->fd, max, strlen(max) + 1); // providing those choices

        // alter first second about the first player's turn
        sprintf(max, "\nwaiting for %s to make a move\r\n", first_p->name);
        write(sec_p->fd, max, strlen(max) + 1);
    }
    return 0;
}




int attack_opt(struct client *head, struct client *first_p, struct client *sec_p) {
    char final[256];
    int normal_att = 2 + (rand() % 5);  // Calculate the basic damage for the attack

    if (first_p->status != 0) {
        first_p->status = 0;
    }


    // alert about damage down to both players
    snprintf(final, sizeof(final), "\nYour basic attack dealt a massive %d damage to %s\r\n", normal_att, sec_p->name);
    write(first_p->fd, final, strlen(final) + 1);
    snprintf(final, sizeof(final), "\nYou've suffered %d damage from %s\r\n", normal_att, first_p->name);
    write(sec_p->fd, final, strlen(final) + 1);

    // get new damage
    sec_p->clash.hp =  sec_p->clash.hp - normal_att;

    // check if it finished game and fetch opponents accordingly
    if (!(sec_p->clash.hp > 0)) {
        losing_situation(sec_p, first_p);

        int againt_p1 = opp_connector(head, first_p);
        int against_p2 = opp_connector(head,  sec_p);

        if (againt_p1 != 0 && (first_p->clash.progress_game == 0 || first_p->clash.progress_game == 1)) {
            information(first_p, (first_p->clash).curr_opponent);
            player_choices(first_p, (first_p->clash).curr_opponent);
        }
        if (against_p2 != 0 && (sec_p->clash.progress_game == 0 || sec_p->clash.progress_game == 1)) {
            information(sec_p, (sec_p->clash).curr_opponent);
            player_choices(sec_p, (sec_p->clash).curr_opponent);
        }

        return 0;
    } else {
        // Switch turn accordingly
        if (sec_p->status != 1) {
            sec_p->status = 1;
            information(first_p, sec_p);
            player_choices(first_p, sec_p);
        }
    }

    return 0;
}






int pmove_opt(struct client *head, struct client *first_p, struct client *sec_p) {

    char final[256];
    int normal_att = 2 + (rand() % 5);

    if (first_p->status != 0) {
        first_p->status = 0;
    }

    if ((first_p->clash).pmoves > 0) {
        (first_p->clash).pmoves -= 1;
    }

    // Determine if the power move results in a critical hit or a miss
    int determine = rand() % 2;

    if (determine != 0) {
        // get new hit value
        int total_hit = 3 * normal_att;

        snprintf(final, sizeof(final), "\nCRITICAL HIT! %d DAMAGE DEALT TO %s\r\n", total_hit, sec_p->name);
        write(first_p->fd, final, strlen(final) + 1);
        snprintf(final, sizeof(final), "\nYou took CRITICAL DAMAGE of %d from %s\r\n", total_hit, first_p->name);
        write(sec_p->fd, final, strlen(final) + 1);

        sec_p->clash.hp -= (3 * normal_att);


        // checking if the hit finished game
        if ((first_p->clash).progress_game == 1 && !((sec_p->clash).hp > 0)) {
            losing_situation(sec_p, first_p);

            // getting new opponnents when available for both
            int againt_p1 = opp_connector(head, first_p);
            int against_p2 = opp_connector(head,  sec_p);

            if (againt_p1 != 0 && (first_p->clash.progress_game == 0 || first_p->clash.progress_game == 1)) {
                information(first_p, (first_p->clash).curr_opponent);
                player_choices(first_p, (first_p->clash).curr_opponent);
            }
            if (against_p2 != 0 && (sec_p->clash.progress_game == 0 || sec_p->clash.progress_game == 1)) {
                information(sec_p, (sec_p->clash).curr_opponent);
                player_choices(sec_p, (sec_p->clash).curr_opponent);
            }

        } else {
            // switching turns accordingly
            if (sec_p->status != 1) {
                sec_p->status = 1;
                information(first_p, sec_p);
                player_choices(first_p, sec_p);
            }
        }
    } else {

        // giving correct output for miss
        snprintf(final, sizeof(final), "\nYou missed\r\n");
        write(first_p->fd, final, strlen(final) + 1);
        snprintf(final, sizeof(final), "\n%s missed their critical hit\r\n", first_p->name);
        write(sec_p->fd, final, strlen(final) + 1);

        //changing to second player turn
        if (sec_p->status != 1) {
            sec_p->status = 1;
            information(first_p, sec_p);
            player_choices(first_p, sec_p);
        }
    }

    return 0;
}



int hmove_opt(struct client *head, struct client *first_p, struct client *sec_p) {
    char final_out[256];

    if (first_p->status != 0) {
        first_p->status = 0;
    }

// provide correct statement for first player that they have used this move
    snprintf(final_out, sizeof(final_out), "\nYou used to swap hp with %s, but lost 3 hp in the process\r\n", sec_p->name);
    write(first_p->fd, final_out, strlen(final_out) + 1);

// alert second player about first players choice
    snprintf(final_out, sizeof(final_out), "\n%s used to swap hp with you, but %s lost 3 hp in the process\r\n", first_p->name, first_p->name);
    write(sec_p->fd, final_out, strlen(final_out) + 1);

// swapping the health but subtracting by 3

    int temphp = sec_p->clash.hp;
    sec_p->clash.hp = first_p->clash.hp;
    first_p->clash.hp = temphp - 3;

    if ((first_p->clash).hp <= 0) { // if leads to going below their hp when performed
        losing_situation(first_p, sec_p);

        int againt_p1 = opp_connector(head, first_p);
        int against_p2 = opp_connector(head,  sec_p);

        if (againt_p1 != 0 && (first_p->clash.progress_game == 0 || first_p->clash.progress_game == 1)) {
            information(first_p, (first_p->clash).curr_opponent);
            player_choices(first_p, (first_p->clash).curr_opponent);
        }
        if (against_p2 != 0 && (sec_p->clash.progress_game == 0 || sec_p->clash.progress_game == 1)) {
            information(sec_p, (sec_p->clash).curr_opponent);
            player_choices(sec_p, (sec_p->clash).curr_opponent);
        }
        return 0;

    } else {
        if (sec_p->status != 1) {
            sec_p->status = 1;
            information(first_p, sec_p);
            player_choices(first_p, sec_p);
        }
    }
    return 0;
}



void losing_situation(struct client *first_p, struct client *sec_p) {
    char final_total[256];

    // correct message output according to which player won and lost
    snprintf(final_total, sizeof(final_total), "\nYou lost the game with your strategy\r\n");
    write(first_p->fd, final_total, strlen(final_total) + 1);

    snprintf(final_total, sizeof(final_total), "\n%s lost the game with their strategy, you won\r\n", first_p->name);
    write(sec_p->fd, final_total, strlen(final_total) + 1);

    // checking to see if first player in match
    if (first_p->clash.progress_game != 0) {
        first_p->clash.progress_game = 0;
    }

    // checking to see if second player in match
    if (sec_p->clash.progress_game != 0) {
        sec_p->clash.progress_game = 0;
    }

    // checking to the status of seconf player and resetting it
    if (sec_p->status != 0) {
        sec_p->status = 0;
    }

    // game ended and give a correct state to wait for new players if not left
    snprintf(final_total, sizeof(final_total), "\n...Waiting for the next opponent...\r\n");
    write(first_p->fd, final_total, strlen(final_total) + 1);
    write(sec_p->fd, final_total, strlen(final_total) + 1);
}




int convert_inp(struct client *head, struct client *chosen) {
    int total_bt, action = -5;
    char final_out[256];

    total_bt = read(chosen->fd, (chosen->inp_handle).inp_next, (chosen->inp_handle).inp_space);
    if (total_bt == -1) {
        perror("gotten read issue");
        return -1;
    } else if (total_bt == 0) {
        removeclient(head, chosen->fd);
        return 0;
    }

    switch (chosen->inp_handle.buff_inp[0]) {  //having the input command
        case 'a':
            action = 10;
            break;
        case 'p':
            if (chosen->clash.pmoves > 0) action = 20;  // the power moves number check
            break;
        case 'h':
            action = 30;
            break;
        case 's':
            if (!chosen->speak) {
                chosen->speak = 1;
                fixing_msgbf(chosen);

                // giving the message for them to write and we process
                strcpy(final_out, "\nMessage to be sent to opponent: \r\n");
                write(chosen->fd, final_out, strlen(final_out));

                filter_inp(chosen);
            }
            break;
        default:
            action = -5;
    }

    //  calling the proper action called

    if (action == 10) {
        return attack_opt(head, chosen, chosen->clash.curr_opponent);
    } else if (action == 20){
        return pmove_opt(head, chosen, chosen->clash.curr_opponent);
    } else if (action == 30){
        return hmove_opt(head, chosen, chosen->clash.curr_opponent);
    }
    //Reverting the command buffer
    revert_choicebf(chosen);

    return 0;
}

void fixing_msgbf(struct client *p) {
    if (p->direct_handle.space != sizeof(p->direct_handle.buf_message)) {
        p->direct_handle.space = sizeof(p->direct_handle.buf_message);
    }

    if (p->direct_handle.check_mbuf != 0) {
        p->direct_handle.check_mbuf = 0;
    }

    if (p->direct_handle.next != p->direct_handle.buf_message) {
        p->direct_handle.next = p->direct_handle.buf_message;
    }
}

void revert_choicebf(struct client *p) {


    if (p->inp_handle.inp_space != sizeof(p->inp_handle.buff_inp)) {
        p->inp_handle.inp_space = sizeof(p->inp_handle.buff_inp);
    }

    if (p->inp_handle.check_inpbuff != 0) {
        p->inp_handle.check_inpbuff = 0;
    }

    if (p->inp_handle.inp_next != p->inp_handle.buff_inp) {
        p->inp_handle.inp_next = p->inp_handle.buff_inp;
    }
}
//helpers for buffer adjustemnt and reset