/*
 * HOSPITAL MANAGEMENT SYSTEM  (pure C, console based)
 * ---------------------------------------------------
 * Modules:
 *   1. Patients      -> add / view / search / update / discharge(delete)
 *   2. Doctors       -> add / view
 *   3. Appointments  -> book (with clash check) / view / cancel
 *   4. Billing       -> generate bill / view bills
 *
 * Storage: every module has its own binary file (patients.dat etc.),
 * so data stays saved after the program closes.
 *
 * Key trick: every record struct starts with an `int id`, and ids are
 * 1,2,3... so record number N lives at byte offset (N-1)*sizeof(record).
 * That lets one set of generic helper functions serve ALL modules.
 *
 * Compile:  gcc -Wall -o hospital hospital.c
 * Run:      ./hospital
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- File names ---------- */
#define PATIENT_FILE "patients.dat"
#define DOCTOR_FILE  "doctors.dat"
#define APPT_FILE    "appointments.dat"
#define BILL_FILE    "bills.dat"

#define ROOM_RATE 1000.0f   /* room charge per day (Rs.) */

/* ---------- Data structures (one per entity) ---------- */
typedef struct {
    int  id;               /* MUST stay the first member (generic helpers rely on it) */
    char name[50];
    int  age;
    char gender;           /* 'M', 'F' or 'O' */
    char disease[50];
    char phone[15];
    int  active;           /* 1 = admitted/registered, 0 = deleted (soft delete) */
} Patient;

typedef struct {
    int   id;
    char  name[50];
    char  specialization[40];
    float fee;             /* consultation fee */
} Doctor;

typedef struct {
    int  id;
    int  patientId;
    int  doctorId;
    char date[11];         /* "DD-MM-YYYY" */
    int  slot;             /* 1..8  => 9am..5pm hourly slots */
    int  booked;           /* 1 = booked, 0 = cancelled */
} Appointment;

typedef struct {
    int   id;
    int   patientId;
    float consultFee;
    float medicineCost;
    float roomCost;
    float total;
} Bill;

/* ================= INPUT HELPERS =================
 * scanf leaves '\n' in the buffer and breaks later reads, so we always
 * read a whole line with fgets and convert it ourselves. */

/* Read a line of text, strip the trailing newline. */
void readLine(const char *prompt, char *buf, int size) {
    printf("%s", prompt);
    if (fgets(buf, size, stdin) == NULL) { buf[0] = '\0'; return; }
    buf[strcspn(buf, "\n")] = '\0';
}

/* Read an integer (re-asks until user types a valid number). */
int readInt(const char *prompt) {
    char line[32];
    int value;
    while (1) {
        readLine(prompt, line, sizeof line);
        if (sscanf(line, "%d", &value) == 1) return value;
        printf("  Please enter a valid number.\n");
    }
}

/* Read a float the same way. */
float readFloat(const char *prompt) {
    char line[32];
    float value;
    while (1) {
        readLine(prompt, line, sizeof line);
        if (sscanf(line, "%f", &value) == 1) return value;
        printf("  Please enter a valid amount.\n");
    }
}

/* ================= GENERIC FILE HELPERS ================= */

/* How many records exist in a file? (file size / record size) */
int countRecords(const char *file, size_t size) {
    FILE *fp = fopen(file, "rb");
    if (!fp) return 0;                       /* file not created yet */
    fseek(fp, 0, SEEK_END);
    long bytes = ftell(fp);
    fclose(fp);
    return (int)(bytes / (long)size);
}

/* Next free id = current count + 1 (ids never get reused). */
int nextId(const char *file, size_t size) {
    return countRecords(file, size) + 1;
}

/* Append a new record at the end of the file. */
void appendRecord(const char *file, const void *rec, size_t size) {
    FILE *fp = fopen(file, "ab");
    if (!fp) { printf("Cannot open %s\n", file); return; }
    fwrite(rec, size, 1, fp);
    fclose(fp);
}

/* Load record with given id into `out`. Returns 1 if found, 0 if not. */
int loadRecord(const char *file, size_t size, int id, void *out) {
    if (id < 1 || id > countRecords(file, size)) return 0;
    FILE *fp = fopen(file, "rb");
    if (!fp) return 0;
    fseek(fp, (long)(id - 1) * (long)size, SEEK_SET);  /* jump straight to it */
    int ok = (fread(out, size, 1, fp) == 1);
    fclose(fp);
    return ok;
}

/* Overwrite the record with given id (used for update / delete / cancel). */
void saveRecord(const char *file, size_t size, int id, const void *rec) {
    FILE *fp = fopen(file, "rb+");           /* rb+ = read/write, keep contents */
    if (!fp) return;
    fseek(fp, (long)(id - 1) * (long)size, SEEK_SET);
    fwrite(rec, size, 1, fp);
    fclose(fp);
}

/* ================= PATIENT MODULE ================= */

void addPatient(void) {
    Patient p;
    p.id = nextId(PATIENT_FILE, sizeof(Patient));
    readLine("Name        : ", p.name, sizeof p.name);
    p.age = readInt("Age         : ");
    char g[8];
    readLine("Gender (M/F/O): ", g, sizeof g);
    p.gender = (g[0] >= 'a' && g[0] <= 'z') ? g[0] - 32 : g[0];  /* to uppercase */
    readLine("Disease     : ", p.disease, sizeof p.disease);
    readLine("Phone       : ", p.phone, sizeof p.phone);
    p.active = 1;
    appendRecord(PATIENT_FILE, &p, sizeof p);
    printf("Patient registered. Patient ID = %d\n", p.id);
}

/* Print one patient as a table row. */
void printPatient(const Patient *p) {
    printf("%-4d %-20s %-4d %-3c %-18s %-12s\n",
           p->id, p->name, p->age, p->gender, p->disease, p->phone);
}

void viewPatients(void) {
    int n = countRecords(PATIENT_FILE, sizeof(Patient));
    Patient p;
    printf("\n%-4s %-20s %-4s %-3s %-18s %-12s\n",
           "ID", "Name", "Age", "G", "Disease", "Phone");
    int shown = 0;
    for (int i = 1; i <= n; i++) {
        if (loadRecord(PATIENT_FILE, sizeof p, i, &p) && p.active) {
            printPatient(&p);
            shown++;
        }
    }
    if (!shown) printf("No patients found.\n");
}

/* Search by id OR by (partial) name. */
void searchPatient(void) {
    char key[50];
    readLine("Enter patient ID or name to search: ", key, sizeof key);
    int n = countRecords(PATIENT_FILE, sizeof(Patient));
    Patient p;
    int found = 0, asId = atoi(key);
    for (int i = 1; i <= n; i++) {
        if (!loadRecord(PATIENT_FILE, sizeof p, i, &p) || !p.active) continue;
        if ((asId > 0 && p.id == asId) || strstr(p.name, key) != NULL) {
            if (!found)
                printf("\n%-4s %-20s %-4s %-3s %-18s %-12s\n",
                       "ID", "Name", "Age", "G", "Disease", "Phone");
            printPatient(&p);
            found = 1;
        }
    }
    if (!found) printf("No match.\n");
}

void updatePatient(void) {
    int id = readInt("Patient ID to update: ");
    Patient p;
    if (!loadRecord(PATIENT_FILE, sizeof p, id, &p) || !p.active) {
        printf("Patient not found.\n");
        return;
    }
    char tmp[50];
    /* Pressing Enter on a field keeps the old value. */
    readLine("New disease (Enter = keep): ", tmp, sizeof tmp);
    if (tmp[0]) strcpy(p.disease, tmp);
    readLine("New phone   (Enter = keep): ", tmp, sizeof tmp);
    if (tmp[0]) strncpy(p.phone, tmp, sizeof p.phone - 1);
    saveRecord(PATIENT_FILE, sizeof p, id, &p);
    printf("Patient updated.\n");
}

/* Soft delete: keep the record but mark it inactive so ids stay valid. */
void deletePatient(void) {
    int id = readInt("Patient ID to discharge/delete: ");
    Patient p;
    if (!loadRecord(PATIENT_FILE, sizeof p, id, &p) || !p.active) {
        printf("Patient not found.\n");
        return;
    }
    p.active = 0;
    saveRecord(PATIENT_FILE, sizeof p, id, &p);
    printf("Patient removed.\n");
}

/* ================= DOCTOR MODULE ================= */

void addDoctor(void) {
    Doctor d;
    d.id = nextId(DOCTOR_FILE, sizeof(Doctor));
    readLine("Doctor name    : ", d.name, sizeof d.name);
    readLine("Specialization : ", d.specialization, sizeof d.specialization);
    d.fee = readFloat("Consultation fee: ");
    appendRecord(DOCTOR_FILE, &d, sizeof d);
    printf("Doctor added. Doctor ID = %d\n", d.id);
}

void viewDoctors(void) {
    int n = countRecords(DOCTOR_FILE, sizeof(Doctor));
    Doctor d;
    printf("\n%-4s %-20s %-20s %-8s\n", "ID", "Name", "Specialization", "Fee");
    if (n == 0) { printf("No doctors yet.\n"); return; }
    for (int i = 1; i <= n; i++)
        if (loadRecord(DOCTOR_FILE, sizeof d, i, &d))
            printf("%-4d %-20s %-20s %-8.2f\n", d.id, d.name, d.specialization, d.fee);
}

/* ================= APPOINTMENT MODULE ================= */

/* Is this doctor already booked on this date & slot? (prevents double booking) */
int slotTaken(int doctorId, const char *date, int slot) {
    int n = countRecords(APPT_FILE, sizeof(Appointment));
    Appointment a;
    for (int i = 1; i <= n; i++) {
        if (loadRecord(APPT_FILE, sizeof a, i, &a) &&
            a.booked && a.doctorId == doctorId &&
            a.slot == slot && strcmp(a.date, date) == 0)
            return 1;
    }
    return 0;
}

void bookAppointment(void) {
    Appointment a;
    Patient p;
    Doctor d;
    a.patientId = readInt("Patient ID : ");
    if (!loadRecord(PATIENT_FILE, sizeof p, a.patientId, &p) || !p.active) {
        printf("Patient not found.\n");
        return;
    }
    a.doctorId = readInt("Doctor ID  : ");
    if (!loadRecord(DOCTOR_FILE, sizeof d, a.doctorId, &d)) {
        printf("Doctor not found.\n");
        return;
    }
    readLine("Date (DD-MM-YYYY): ", a.date, sizeof a.date);
    a.slot = readInt("Slot 1-8 (1=9am ... 8=4pm): ");
    if (a.slot < 1 || a.slot > 8) { printf("Invalid slot.\n"); return; }

    if (slotTaken(a.doctorId, a.date, a.slot)) {
        printf("Sorry, Dr. %s is already booked at that time.\n", d.name);
        return;
    }
    a.id = nextId(APPT_FILE, sizeof a);
    a.booked = 1;
    appendRecord(APPT_FILE, &a, sizeof a);
    printf("Appointment booked. Appointment ID = %d\n", a.id);
}

void viewAppointments(void) {
    int n = countRecords(APPT_FILE, sizeof(Appointment));
    Appointment a;
    Patient p;
    Doctor d;
    printf("\n%-4s %-18s %-18s %-11s %-5s\n", "ID", "Patient", "Doctor", "Date", "Slot");
    int shown = 0;
    for (int i = 1; i <= n; i++) {
        if (!loadRecord(APPT_FILE, sizeof a, i, &a) || !a.booked) continue;
        loadRecord(PATIENT_FILE, sizeof p, a.patientId, &p);
        loadRecord(DOCTOR_FILE, sizeof d, a.doctorId, &d);
        printf("%-4d %-18s %-18s %-11s %-5d\n", a.id, p.name, d.name, a.date, a.slot);
        shown++;
    }
    if (!shown) printf("No active appointments.\n");
}

void cancelAppointment(void) {
    int id = readInt("Appointment ID to cancel: ");
    Appointment a;
    if (!loadRecord(APPT_FILE, sizeof a, id, &a) || !a.booked) {
        printf("Appointment not found.\n");
        return;
    }
    a.booked = 0;                         /* soft cancel, frees the slot */
    saveRecord(APPT_FILE, sizeof a, id, &a);
    printf("Appointment cancelled.\n");
}

/* ================= BILLING MODULE ================= */

void generateBill(void) {
    Bill b;
    Patient p;
    b.patientId = readInt("Patient ID: ");
    if (!loadRecord(PATIENT_FILE, sizeof p, b.patientId, &p) || !p.active) {
        printf("Patient not found.\n");
        return;
    }
    int docId = readInt("Doctor ID consulted (0 = none): ");
    Doctor d;
    b.consultFee = 0;
    if (docId > 0 && loadRecord(DOCTOR_FILE, sizeof d, docId, &d))
        b.consultFee = d.fee;             /* fee comes from doctor record */

    b.medicineCost = readFloat("Medicine charges: ");
    int days = readInt("Room days (0 if outpatient): ");
    b.roomCost = days * ROOM_RATE;
    b.total = b.consultFee + b.medicineCost + b.roomCost;
    b.id = nextId(BILL_FILE, sizeof b);
    appendRecord(BILL_FILE, &b, sizeof b);

    /* Print the receipt */
    printf("\n======== BILL #%d ========\n", b.id);
    printf("Patient      : %s (ID %d)\n", p.name, p.id);
    printf("Consultation : %.2f\n", b.consultFee);
    printf("Medicines    : %.2f\n", b.medicineCost);
    printf("Room (%d d)  : %.2f\n", days, b.roomCost);
    printf("-------------------------\n");
    printf("TOTAL        : %.2f\n", b.total);
    printf("=========================\n");
}

void viewBills(void) {
    int n = countRecords(BILL_FILE, sizeof(Bill));
    Bill b;
    printf("\n%-4s %-10s %-12s\n", "ID", "PatientID", "Total");
    if (n == 0) { printf("No bills yet.\n"); return; }
    for (int i = 1; i <= n; i++)
        if (loadRecord(BILL_FILE, sizeof b, i, &b))
            printf("%-4d %-10d %-12.2f\n", b.id, b.patientId, b.total);
}

/* ================= MENUS ================= */

void patientMenu(void) {
    int c;
    do {
        printf("\n--- PATIENTS ---\n1.Add 2.View all 3.Search 4.Update 5.Delete 0.Back\n");
        c = readInt("Choice: ");
        switch (c) {
            case 1: addPatient();    break;
            case 2: viewPatients();  break;
            case 3: searchPatient(); break;
            case 4: updatePatient(); break;
            case 5: deletePatient(); break;
            case 0: break;
            default: printf("Invalid choice.\n");
        }
    } while (c != 0);
}

void doctorMenu(void) {
    int c;
    do {
        printf("\n--- DOCTORS ---\n1.Add 2.View all 0.Back\n");
        c = readInt("Choice: ");
        switch (c) {
            case 1: addDoctor();   break;
            case 2: viewDoctors(); break;
            case 0: break;
            default: printf("Invalid choice.\n");
        }
    } while (c != 0);
}

void appointmentMenu(void) {
    int c;
    do {
        printf("\n--- APPOINTMENTS ---\n1.Book 2.View 3.Cancel 0.Back\n");
        c = readInt("Choice: ");
        switch (c) {
            case 1: bookAppointment();   break;
            case 2: viewAppointments();  break;
            case 3: cancelAppointment(); break;
            case 0: break;
            default: printf("Invalid choice.\n");
        }
    } while (c != 0);
}

void billingMenu(void) {
    int c;
    do {
        printf("\n--- BILLING ---\n1.Generate bill 2.View bills 0.Back\n");
        c = readInt("Choice: ");
        switch (c) {
            case 1: generateBill(); break;
            case 2: viewBills();    break;
            case 0: break;
            default: printf("Invalid choice.\n");
        }
    } while (c != 0);
}

/* ================= MAIN ================= */

int main(void) {
    int c;
    do {
        printf("\n===== HOSPITAL MANAGEMENT SYSTEM =====\n");
        printf("1.Patients  2.Doctors  3.Appointments  4.Billing  0.Exit\n");
        c = readInt("Choice: ");
        switch (c) {
            case 1: patientMenu();     break;
            case 2: doctorMenu();      break;
            case 3: appointmentMenu(); break;
            case 4: billingMenu();     break;
            case 0: printf("Goodbye!\n"); break;
            default: printf("Invalid choice.\n");
        }
    } while (c != 0);
    return 0;
}
