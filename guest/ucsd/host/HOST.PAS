PROGRAM HOST;
(*$I-*)

TYPE
  TByte = 0..255;
  TQuad = PACKED ARRAY[1..4] OF TByte;
  TCommand = STRING[80];
  TBlock = PACKED ARRAY[0..511] OF CHAR;
  TDirBuf = PACKED ARRAY[0..2047] OF CHAR;
  TFileNm = STRING[15];

VAR
  CmdLine: TCommand;
  Verb: TCommand;
  Argument: TCommand;
  Option: TCommand;
  BlkFile: FILE;
  PendName: ARRAY[1..24] OF TFileNm;
  PendTail: ARRAY[1..24] OF INTEGER;
  PendCnt: INTEGER;
  BlkBuf: TBlock;
  DirBuf: TDirBuf;
  CrcValue: TQuad;
  ExpCrc: TQuad;
  FileSize: TQuad;
  IoError: BOOLEAN;
  ExchOK: BOOLEAN;

FUNCTION EXCHREAD: INTEGER; EXTERNAL;
PROCEDURE EXCHWRT(Value: INTEGER); EXTERNAL;
PROCEDURE EXCHABT; EXTERNAL;
PROCEDURE EXCHRDB(VAR Buf: TBlock; Count: INTEGER); EXTERNAL;
PROCEDURE EXCHWRB(VAR Buf: TBlock; Count: INTEGER); EXTERNAL;
PROCEDURE EXCHCRC(VAR Crc: TQuad; VAR Buf: TBlock; Count: INTEGER); EXTERNAL;

PROCEDURE InitCrc(VAR Value: TQuad);
VAR
  Counter: INTEGER;
BEGIN
  FOR Counter := 1 TO 4 DO
    Value[Counter] := 255
END;

PROCEDURE EndCrc(VAR Value: TQuad);
VAR
  Counter: INTEGER;
BEGIN
  FOR Counter := 1 TO 4 DO
    Value[Counter] := 255 - Value[Counter]
END;

PROCEDURE ZeroQuad(VAR Value: TQuad);
VAR
  Counter: INTEGER;
BEGIN
  FOR Counter := 1 TO 4 DO
    Value[Counter] := 0
END;

FUNCTION SameQuad(LeftVal, RightVal: TQuad): BOOLEAN;
VAR
  Counter: INTEGER;
BEGIN
  SameQuad := TRUE;
  FOR Counter := 1 TO 4 DO
    IF LeftVal[Counter] <> RightVal[Counter] THEN
      SameQuad := FALSE
END;

PROCEDURE SendQuad(Value: TQuad);
VAR
  Counter: INTEGER;
BEGIN
  FOR Counter := 1 TO 4 DO
    EXCHWRT(Value[Counter])
END;

PROCEDURE RecvQuad(VAR Value: TQuad);
VAR
  Counter: INTEGER;
BEGIN
  FOR Counter := 1 TO 4 DO
    Value[Counter] := EXCHREAD
END;

PROCEDURE LenQuad(Blocks, LastByt: INTEGER; VAR Value: TQuad);
VAR
  Full: INTEGER;
  Low: INTEGER;
  High: INTEGER;
BEGIN
  IF Blocks = 0 THEN
    ZeroQuad(Value)
  ELSE
  BEGIN
    Full := (Blocks - 1) * 2;
    Low := (Full MOD 256) + (LastByt DIV 256);
    High := Full DIV 256;
    IF Low >= 256 THEN
    BEGIN
      Low := Low - 256;
      High := High + 1
    END;
    Value[1] := LastByt MOD 256;
    Value[2] := Low;
    Value[3] := High MOD 256;
    Value[4] := High DIV 256
  END
END;

PROCEDURE QuadParts(Value: TQuad; VAR Chunks: INTEGER; VAR Rest: INTEGER;
                    VAR Fits: BOOLEAN);
BEGIN
  Fits := (Value[4] = 0) AND (Value[3] < 128);
  Rest := Value[1];
  Chunks := 0;
  IF Fits THEN
    Chunks := Value[2] + (256 * Value[3])
END;

FUNCTION UpperChr(Value: CHAR): CHAR;
BEGIN
  IF (Value >= 'a') AND (Value <= 'z') THEN
    UpperChr := CHR(ORD(Value) - 32)
  ELSE
    UpperChr := Value
END;

PROCEDURE UpString(VAR Value: TCommand);
VAR
  Counter: INTEGER;
BEGIN
  FOR Counter := 1 TO LENGTH(Value) DO
    Value[Counter] := UpperChr(Value[Counter])
END;

FUNCTION ValidNm(Value: TCommand): BOOLEAN;
VAR
  Counter: INTEGER;
  CharVal: CHAR;
  IsValid: BOOLEAN;
BEGIN
  IsValid := (LENGTH(Value) >= 1) AND (LENGTH(Value) <= 15);
  IF IsValid THEN
  BEGIN
    IF NOT ((Value[1] >= 'A') AND (Value[1] <= 'Z')) THEN
      IsValid := FALSE;
    FOR Counter := 1 TO LENGTH(Value) DO
    BEGIN
      CharVal := Value[Counter];
      IF NOT (((CharVal >= 'A') AND (CharVal <= 'Z')) OR
              ((CharVal >= '0') AND (CharVal <= '9')) OR
              (CharVal = '.')) THEN
        IsValid := FALSE
    END
  END;
  ValidNm := IsValid
END;

PROCEDURE GetToken(VAR Text: TCommand; VAR Cursor: INTEGER;
                   VAR Token: TCommand);
VAR
  Done: BOOLEAN;
BEGIN
  Token := '';
  Done := FALSE;
  WHILE (Cursor <= LENGTH(Text)) AND NOT Done DO
  BEGIN
    IF Text[Cursor] = ' ' THEN
      Cursor := Cursor + 1
    ELSE
      Done := TRUE
  END;
  Done := FALSE;
  WHILE (Cursor <= LENGTH(Text)) AND NOT Done DO
  BEGIN
    IF Text[Cursor] = ' ' THEN
      Done := TRUE
    ELSE
    BEGIN
      Token := CONCAT(Token, COPY(Text, Cursor, 1));
      Cursor := Cursor + 1
    END
  END;
END;

PROCEDURE PrStatus(Status: INTEGER);
BEGIN
  CASE Status OF
    1: WRITELN('Invalid name or request.');
    2: WRITELN('File or exchange directory unavailable.');
    3: WRITELN('Destination already exists.');
    4: WRITELN('I/O or checksum error.');
    5: WRITELN('Exchange service busy.')
  OTHERWISE
    WRITELN('Unknown exchange error.')
  END
END;

PROCEDURE QueryExc(VAR Good: BOOLEAN);
VAR
  Status: INTEGER;
  Version: INTEGER;
  Capabil: INTEGER;
BEGIN
  EXCHWRT(0);
  Status := EXCHREAD;
  Version := EXCHREAD;
  Capabil := EXCHREAD;
  Good := (Status = 0) AND (Version = 1) AND ((Capabil MOD 8) = 7);
  IF NOT Good THEN
  BEGIN
    WRITELN('HOST exchange protocol is unavailable or unsupported.');
    IF Status <> 0 THEN
      PrStatus(Status)
  END
END;

PROCEDURE ListXchg;
VAR
  Status: INTEGER;
  CharVal: INTEGER;
  Count: INTEGER;
  NameLength: INTEGER;
BEGIN
  EXCHWRT(1);
  Status := EXCHREAD;
  IF Status <> 0 THEN
  BEGIN
    WRITE('DIR: ');
    PrStatus(Status);
  END;
  IF Status = 0 THEN
  BEGIN
    Count := 0;
    REPEAT
      NameLength := 0;
      CharVal := EXCHREAD;
      WHILE CharVal <> 0 DO
      BEGIN
        WRITE(CHR(CharVal));
        NameLength := NameLength + 1;
        CharVal := EXCHREAD
      END;
      IF NameLength > 0 THEN
      BEGIN
        WRITELN;
        Count := Count + 1
      END
    UNTIL NameLength = 0;
    Status := EXCHREAD;
    IF Status <> 0 THEN
    BEGIN
      WRITE('DIR: ');
      PrStatus(Status)
    END;
    IF (Status = 0) AND (Count = 0) THEN
      WRITELN('No files.')
  END
END;


PROCEDURE FindEntry(Name: TCommand; VAR Found: BOOLEAN; VAR Base: INTEGER);
VAR
  Counter: INTEGER;
  Index: INTEGER;
  NumFiles: INTEGER;
  Same: BOOLEAN;
BEGIN
  Found := FALSE;
  Base := 0;
  NumFiles := ORD(DirBuf[16]) + (256 * ORD(DirBuf[17]));
  IF NumFiles > 77 THEN
    NumFiles := 77;
  Counter := 1;
  WHILE (Counter <= NumFiles) AND NOT Found DO
  BEGIN
    Base := Counter * 26;
    Same := ORD(DirBuf[Base + 6]) = LENGTH(Name);
    Index := 1;
    WHILE Same AND (Index <= LENGTH(Name)) DO
    BEGIN
      IF DirBuf[Base + 6 + Index] <> Name[Index] THEN
        Same := FALSE;
      Index := Index + 1
    END;
    IF Same THEN
      Found := TRUE;
    Counter := Counter + 1
  END
END;

PROCEDURE FileInfo(Name: TCommand; VAR Found: BOOLEAN; VAR Blocks: INTEGER;
                   VAR LastByt: INTEGER; VAR Good: BOOLEAN);
VAR
  Base: INTEGER;
  Got: INTEGER;
BEGIN
  Found := FALSE;
  Good := FALSE;
  Blocks := 0;
  LastByt := 0;
  RESET(BlkFile, ':');
  IF IORESULT = 0 THEN
  BEGIN
    Got := BLOCKREAD(BlkFile, DirBuf, 4, 2);
    IF (Got = 4) AND (IORESULT = 0) THEN
    BEGIN
      Good := TRUE;
      FindEntry(Name, Found, Base);
      IF Found THEN
      BEGIN
        Blocks := (ORD(DirBuf[Base + 2]) + (256 * ORD(DirBuf[Base + 3]))) -
                  (ORD(DirBuf[Base]) + (256 * ORD(DirBuf[Base + 1])));
        LastByt := ORD(DirBuf[Base + 22]) + (256 * ORD(DirBuf[Base + 23]))
      END
    END;
    CLOSE(BlkFile)
  END
END;

PROCEDURE ScanFile(Name: TCommand; Blocks, LastByt: INTEGER; Send: BOOLEAN);
VAR
  BlockNo: INTEGER;
  Count: INTEGER;
  Got: INTEGER;
BEGIN
  IoError := FALSE;
  IF NOT Send THEN
    InitCrc(CrcValue);
  RESET(BlkFile, Name);
  IF IORESULT <> 0 THEN
    IoError := TRUE
  ELSE
  BEGIN
    BlockNo := 1;
    WHILE (BlockNo <= Blocks) AND (NOT IoError) DO
    BEGIN
      Got := BLOCKREAD(BlkFile, BlkBuf, 1);
      IF (Got <> 1) OR (IORESULT <> 0) THEN
        IoError := TRUE
      ELSE
      BEGIN
        IF BlockNo = Blocks THEN
          Count := LastByt
        ELSE
          Count := 512;
        IF Send THEN
          EXCHWRB(BlkBuf, Count)
        ELSE
          EXCHCRC(CrcValue, BlkBuf, Count);
        BlockNo := BlockNo + 1
      END
    END;
    CLOSE(BlkFile);
    IF IORESULT <> 0 THEN
      IoError := TRUE
  END;
  IF (NOT Send) AND (NOT IoError) THEN
    EndCrc(CrcValue)
END;

PROCEDURE NoteLast(Name: TCommand; Tail: INTEGER; VAR Good: BOOLEAN);
VAR
  Slot: INTEGER;
  Counter: INTEGER;
BEGIN
  Good := TRUE;
  Slot := 0;
  FOR Counter := 1 TO PendCnt DO
    IF PendName[Counter] = Name THEN
      Slot := Counter;
  IF (Slot = 0) AND (Tail > 0) THEN
  BEGIN
    IF PendCnt < 24 THEN
    BEGIN
      PendCnt := PendCnt + 1;
      Slot := PendCnt;
      PendName[Slot] := Name
    END
    ELSE
      Good := FALSE
  END;
  IF Slot > 0 THEN
  BEGIN
    IF Tail > 0 THEN
      PendTail[Slot] := Tail
    ELSE
      PendTail[Slot] := 512
  END
END;

PROCEDURE ApplyLast(VAR Good: BOOLEAN);
VAR
  Found: BOOLEAN;
  Blocks: INTEGER;
  LastByt: INTEGER;
  Counter: INTEGER;
  Base: INTEGER;
  Got: INTEGER;
BEGIN
  Good := TRUE;
  IF PendCnt > 0 THEN
  BEGIN
    FileInfo('', Found, Blocks, LastByt, Good);
    IF Good THEN
    BEGIN
      FOR Counter := 1 TO PendCnt DO
      BEGIN
        FindEntry(PendName[Counter], Found, Base);
        IF Found THEN
        BEGIN
          DirBuf[Base + 22] := CHR(PendTail[Counter] MOD 256);
          DirBuf[Base + 23] := CHR(PendTail[Counter] DIV 256)
        END
      END;
      RESET(BlkFile, ':');
      Good := IORESULT = 0;
      IF Good THEN
      BEGIN
        Got := BLOCKWRITE(BlkFile, DirBuf, 4, 2);
        Good := (Got = 4) AND (IORESULT = 0);
        CLOSE(BlkFile);
        IF IORESULT <> 0 THEN
          Good := FALSE
      END
    END
  END
END;

PROCEDURE RecvPart(Count: INTEGER);
VAR
  Got: INTEGER;
BEGIN
  IF Count < 512 THEN
    FILLCHAR(BlkBuf, 512, CHR(0));
  EXCHRDB(BlkBuf, Count);
  EXCHCRC(CrcValue, BlkBuf, Count);
  Got := BLOCKWRITE(BlkFile, BlkBuf, 1);
  IF (Got <> 1) OR (IORESULT <> 0) THEN
    IoError := TRUE
END;

PROCEDURE RecvFile(Name: TCommand; Overwrte: BOOLEAN);
VAR
  Status: INTEGER;
  Chunks: INTEGER;
  Rest: INTEGER;
  Counter: INTEGER;
  FullBlks: INTEGER;
  Tail: INTEGER;
  Blocks: INTEGER;
  LastByt: INTEGER;
  Found: BOOLEAN;
  Good: BOOLEAN;
  Fits: BOOLEAN;
  CanContinue: BOOLEAN;
  Touched: BOOLEAN;
  Stored: BOOLEAN;
  Applied: BOOLEAN;
  Noted: BOOLEAN;
BEGIN
  CanContinue := TRUE;
  Touched := FALSE;
  Stored := FALSE;
  Noted := TRUE;
  FileInfo(Name, Found, Blocks, LastByt, Good);
  IF NOT Good THEN
  BEGIN
    WRITELN('GET: cannot check the local destination.');
    CanContinue := FALSE
  END
  ELSE IF Found AND NOT Overwrte THEN
  BEGIN
    WRITELN('GET: destination already exists; specify O to replace it.');
    CanContinue := FALSE
  END;
  IF CanContinue THEN
  BEGIN
    EXCHWRT(2);
    FOR Counter := 1 TO LENGTH(Name) DO
      EXCHWRT(ORD(Name[Counter]));
    EXCHWRT(0);
    Status := EXCHREAD;
    IF Status <> 0 THEN
    BEGIN
      WRITE('GET: ');
      PrStatus(Status);
      CanContinue := FALSE
    END
  END;
  IF CanContinue THEN
  BEGIN
    RecvQuad(FileSize);
    QuadParts(FileSize, Chunks, Rest, Fits);
    IF NOT Fits THEN
    BEGIN
      EXCHABT;
      WRITELN('GET: file is too large for this utility.');
      CanContinue := FALSE
    END
  END;
  IF CanContinue THEN
  BEGIN
    REWRITE(BlkFile, Name);
    Touched := TRUE;
    IF IORESULT <> 0 THEN
    BEGIN
      EXCHABT;
      WRITELN('GET: cannot create the local file.');
      CanContinue := FALSE
    END
  END;
  IF CanContinue THEN
  BEGIN
    IoError := FALSE;
    InitCrc(CrcValue);
    FullBlks := Chunks DIV 2;
    Tail := ((Chunks MOD 2) * 256) + Rest;
    Counter := 1;
    WHILE (Counter <= FullBlks) AND (NOT IoError) DO
    BEGIN
      RecvPart(512);
      Counter := Counter + 1
    END;
    IF (NOT IoError) AND (Tail > 0) THEN
      RecvPart(Tail);
    IF IoError THEN
    BEGIN
      EXCHABT;
      CLOSE(BlkFile);
      WRITELN('GET: local file error; nothing was stored.')
    END
    ELSE
    BEGIN
      RecvQuad(ExpCrc);
      Status := EXCHREAD;
      EndCrc(CrcValue);
      IF Status <> 0 THEN
      BEGIN
        CLOSE(BlkFile);
        WRITE('GET: ');
        PrStatus(Status)
      END
      ELSE IF NOT SameQuad(CrcValue, ExpCrc) THEN
      BEGIN
        CLOSE(BlkFile);
        WRITELN('GET: checksum error; nothing was stored.')
      END
      ELSE
      BEGIN
        CLOSE(BlkFile, LOCK);
        IF IORESULT <> 0 THEN
          WRITELN('GET: could not store the verified file.')
        ELSE
        BEGIN
          NoteLast(Name, Tail, Noted);
          Stored := TRUE
        END
      END
    END
  END;
  IF Touched THEN
  BEGIN
    ApplyLast(Applied);
    IF Stored THEN
    BEGIN
      IF NOT (Applied AND Noted) THEN
        WRITELN('GET: stored, but the exact length could not be set.');
      WRITELN('GET ', Name, ' ... Done')
    END
    ELSE IF NOT Applied THEN
      WRITELN('GET: could not restore the exact lengths of earlier files.')
  END
END;

PROCEDURE SendFile(Name: TCommand; Overwrte: BOOLEAN);
VAR
  Status: INTEGER;
  Counter: INTEGER;
  Blocks: INTEGER;
  LastByt: INTEGER;
  Found: BOOLEAN;
  Good: BOOLEAN;
BEGIN
  FileInfo(Name, Found, Blocks, LastByt, Good);
  IF NOT (Good AND Found) THEN
    WRITELN('PUT: cannot read the local file.')
  ELSE IF Blocks > 16000 THEN
    WRITELN('PUT: file is too large for this utility.')
  ELSE
  BEGIN
    LenQuad(Blocks, LastByt, FileSize);
    ScanFile(Name, Blocks, LastByt, FALSE);
    IF IoError THEN
      WRITELN('PUT: cannot read the local file.')
    ELSE
    BEGIN
      IF Overwrte THEN
        EXCHWRT(4)
      ELSE
        EXCHWRT(3);
      FOR Counter := 1 TO LENGTH(Name) DO
        EXCHWRT(ORD(Name[Counter]));
      EXCHWRT(0);
      SendQuad(FileSize);
      SendQuad(CrcValue);
      Status := EXCHREAD;
      IF Status <> 0 THEN
      BEGIN
        WRITE('PUT: ');
        PrStatus(Status)
      END
      ELSE
      BEGIN
        ScanFile(Name, Blocks, LastByt, TRUE);
        IF IoError THEN
        BEGIN
          EXCHABT;
          WRITELN('PUT: local file changed or could not be read.')
        END
        ELSE
        BEGIN
          Status := EXCHREAD;
          IF Status <> 0 THEN
          BEGIN
            WRITE('PUT: ');
            PrStatus(Status)
          END
          ELSE
            WRITELN('PUT ', Name, ' ... Done')
        END
      END
    END
  END
END;

PROCEDURE RunCmd;
VAR
  Cursor: INTEGER;
  Overwrte: BOOLEAN;
  Valid: BOOLEAN;
  Extra: TCommand;
BEGIN
  UpString(CmdLine);
  Cursor := 1;
  GetToken(CmdLine, Cursor, Verb);
  GetToken(CmdLine, Cursor, Argument);
  GetToken(CmdLine, Cursor, Option);
  GetToken(CmdLine, Cursor, Extra);
  Valid := Extra = '';
  IF Verb <> '' THEN
  BEGIN
    IF (Verb = 'QUIT') OR (Verb = 'Q') THEN
      CmdLine := ''
    ELSE IF Verb = 'DIR' THEN
    BEGIN
      IF (Argument <> '') OR (Option <> '') OR NOT Valid THEN
        WRITELN('Usage: DIR')
      ELSE
        ListXchg
    END
    ELSE IF (Verb <> 'GET') AND (Verb <> 'PUT') THEN
      WRITELN('Use DIR, GET <file> [O], PUT <file> [O], or QUIT.')
    ELSE IF NOT ValidNm(Argument) THEN
      WRITELN('Invalid filename.')
    ELSE IF NOT Valid THEN
      WRITELN('Too many command arguments.')
    ELSE
    BEGIN
      Overwrte := Option = 'O';
      IF (Option <> '') AND NOT Overwrte THEN
        WRITELN('The optional overwrite argument is O.')
      ELSE IF Verb = 'GET' THEN
        RecvFile(Argument, Overwrte)
      ELSE
        SendFile(Argument, Overwrte)
    END
  END
END;

BEGIN
  PendCnt := 0;
  QueryExc(ExchOK);
  IF ExchOK THEN
  BEGIN
    WRITELN('HOST file exchange');
    REPEAT
      WRITE('HOST> ');
      READLN(CmdLine);
      RunCmd
    UNTIL CmdLine = ''
  END
END.
