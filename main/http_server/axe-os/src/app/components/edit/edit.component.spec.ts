import { ComponentFixture, TestBed } from '@angular/core/testing';

import { EditComponent } from './edit.component';
import { provideHttpClient } from '@angular/common/http';
import { provideToastr } from 'ngx-toastr';
import { provideRouter } from '@angular/router';
import { FormControl, FormGroup } from '@angular/forms';
import { provideNoopAnimations } from '@angular/platform-browser/animations';
import { of, throwError } from 'rxjs';
import { SystemApiService } from 'src/app/services/system.service';

describe('EditComponent', () => {
  let component: EditComponent;
  let fixture: ComponentFixture<EditComponent>;

  beforeEach(() => {
    TestBed.configureTestingModule({
      imports: [EditComponent],
      providers: [provideHttpClient(), provideToastr(), provideRouter([]), provideNoopAnimations()]
    });
    fixture = TestBed.createComponent(EditComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });

  function settingsForm() {
    component.form = new FormGroup({
      frequency: new FormControl(600),
      coreVoltage: new FormControl(1180),
      minfanspeed: new FormControl(25),
      display: new FormControl('SSD1306 (128x32)')
    });
  }

  it('saves live voltage/frequency/fan changes without requesting restart', () => {
    settingsForm();
    const update = spyOn(TestBed.inject(SystemApiService), 'updateSystem').and.returnValue(of({}) as any);
    ['frequency', 'coreVoltage', 'minfanspeed'].forEach(k => component.form.controls[k].markAsDirty());
    component.updateSystem();
    expect(update).toHaveBeenCalled();
    expect(component.savedChanges).toBeFalse();
    expect(component.form.pristine).toBeTrue();
  });

  it('preserves a pending display restart across subsequent live saves and failures', () => {
    settingsForm();
    const update = spyOn(TestBed.inject(SystemApiService), 'updateSystem').and.returnValue(of({}) as any);
    component.form.controls['display'].markAsDirty();
    component.updateSystem();
    expect(component.savedChanges).toBeTrue();
    component.form.controls['coreVoltage'].markAsDirty();
    component.updateSystem();
    expect(component.savedChanges).toBeTrue();
    component.form.controls['frequency'].markAsDirty();
    update.and.returnValue(throwError(() => new Error('save failed')));
    component.updateSystem();
    expect(component.savedChanges).toBeTrue();
    expect(component.form.dirty).toBeTrue();
  });

  it('shows external display information instead of local panel controls', () => {
    component.form = new FormGroup({
      frequency: new FormControl(300),
      coreVoltage: new FormControl(1100),
      autofanspeed: new FormControl(true),
      minfanspeed: new FormControl(25),
      manualFanSpeed: new FormControl(70),
      temptarget: new FormControl(60),
      display: new FormControl('SSD1306 (128x32)'),
      rotation: new FormControl(0),
      displayTimeout: new FormControl(-1),
      invertscreen: new FormControl(false),
      statsFrequency: new FormControl(30),
    });
    component.externalDisplay = true;
    component.isProtoMC3 = true;

    fixture.detectChanges();

    const text = fixture.nativeElement.textContent;
    expect(text).toContain('external bonanzaDisplay');
    expect(text).toContain('Voltage changes apply while mining. Frequency changes temporarily pause mining for ASIC warmup.');
    expect(text).not.toContain('Invert Display Colors');
  });
});
